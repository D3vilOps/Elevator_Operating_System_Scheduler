/*
================================================================================
Title        : Elevator Scheduler.cpp
Description  : Elevator Scheduler for working with Elevator_OS by 
             : Eric Rees for CS4352 final project.
Authors      : Triston Schwab (R#11940154), Caleb Brasuell (R#11984197)
             : Matthew Cabrera (R#11802764), Triston Barrientos (R#11688728)
Date         : 5/3/2026
Version      : 1.0
Usage        : 
Notes        : Requires available port, 127.0.0.1:<port> to work
             : Requires use of Unix or Linux system for socket programming.
             : Applies 3 threads to handle the scheduling of the elevators, and 
             : the communication with the API. 
             : Does not hard code a port value. Select elevator function absorbed by the 
             : scheduler thread. Uses SRT (Shortest Remaining Time) scheduling.
C++ Version  : C++ 17 
================================================================================
*/

//Standard CPP libraries
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <thread>
#include <mutex>
#include <chrono>
#include <queue>
#include <atomic>
#include <condition_variable>
#include <unordered_map>

//Unix and Linux libraries for creating the network
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;
using namespace chrono;

static int g_port = 0; //Stores a port number
static vector<string> g_elevatorIDs; //Stores the elevator IDs read from the building file

struct ElevatorInfo {
    string id;
    int lowestFloor;
    int highestFloor;
};

static vector<ElevatorInfo> g_elevatorInfo;

struct Person {
    string id;              //Unique identifier for the person
    int startFloor;         //Floor the person starts on
    int endFloor;           //Floor the person wants to go to
    steady_clock::time_point arrivalTime; //Time the person was received by the input thread
};

struct Assignment {
    string personID;   //Unique identifier for the person
    string elevatorID; //Unique identifier for the elevator
};

//Input for Scheduler Thread
queue<Person> g_inputQueue;         //Queue to store incoming people from the API
mutex g_inputMutex;                 //Mutex to protect access to the input queue
condition_variable g_inputCV;       //Condition variable to signal the scheduler thread

//Output for Scheduler Thread
queue<Assignment> g_outputQueue;    //Queue to store assignments for the output thread
mutex g_outputMutex;                //Mutex to protect access to the output queue
condition_variable g_outputCV;      //Condition variable to signal the output thread

mutex g_cacheMutex;

atomic<bool> g_simDone(false); //Atomic flag to indicate when the simulation is complete

/*
 * sendRequest function: Handles all communication 
 * with the API. Sends requests to the API by creating the socket
 * and connecting to the server, if it fails simply returns an empty string.
 */
string sendRequest(const string &method, const string &path, const string &body) 
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return "";

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(g_port);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&server), sizeof(server)) < 0) {
        close(sock);
        return "";
    }

    ostringstream req;
    req << method << " " << path << " HTTP/1.0\r\n";
    req << "Host: 127.0.0.1:" << g_port << "\r\n";
    req << "Content-Type: text/plain\r\n";
    req << "Content-Length: " << body.size() << "\r\n";
    req << "\r\n";
    req << body;

    string request = req.str();
    send(sock, request.c_str(), request.size(), 0);

    string response;
    char buf[4096];
    int n;
    while ((n = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        response += buf;
    }
    close(sock);
    return response;
}

/*
 * getBody function: Finds and separates out the body in the message.
 */
string getBody(const string &response) {
    auto pos = response.find("\r\n\r\n");
    if (pos == string::npos) return "";
    return response.substr(pos + 4);
}

/*
 * parseField function: Parses values by searching for key=value format.
 */
string parseField(const string &body, const string &key) {
    string search = key + "=";
    auto pos = body.find(search);
    if (pos == string::npos) return "";
    pos += search.size();
    auto end = body.find_first_of(";\r\n", pos);
    if (end == string::npos) return body.substr(pos);
    return body.substr(pos, end - pos);
}

/*
 * safeStoi function: Safely converts a string to an integer.
 */
int safeStoi(const string &s, int defaultVal = 0) 
{
    if (s.empty()) return defaultVal;
    try { return stoi(s); }
    catch(...) { return defaultVal; }
}

/*
 * inputThread function: Retrieves passengers from the API and adds them to the input queue.
 */
void inputThread() 
{
    while (!g_simDone.load()) {
        string personBody = getBody(sendRequest("GET", "/NextInput", ""));

        if (personBody.empty() || personBody.find("NONE") != string::npos) {
            string statusBody = getBody(sendRequest("GET", "/Simulation/check", ""));
            if (statusBody.find("complete") != string::npos) {
                g_simDone.store(true);
                g_inputCV.notify_all();
                g_outputCV.notify_all();
                break;
            }
            this_thread::sleep_for(milliseconds(50));
            continue;
        }

        stringstream ss(personBody);
        string idStr, startStr, endStr;
        getline(ss, idStr, '|');
        getline(ss, startStr, '|');
        getline(ss, endStr, '|');

        auto trim = [](string &s) {
            s.erase(0, s.find_first_not_of(" \t\r\n"));
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
        };
        trim(idStr);
        trim(startStr);
        trim(endStr);

        if (idStr.empty() || startStr.empty() || endStr.empty()) continue;

        Person p;
        p.id = idStr;
        p.startFloor = safeStoi(startStr);
        p.endFloor = safeStoi(endStr);
        p.arrivalTime = steady_clock::now();

        {
            unique_lock<mutex> lock(g_inputMutex);
            g_inputQueue.push(p);
        }
        g_inputCV.notify_one();
    }
}

/*
 * schedulerThread function: Schedules passengers to elevators using SRT.
 */
void schedulerThread() 
{
    static unordered_map<string, string> elevatorStatusCache;
    static auto lastCacheTime = steady_clock::now();

    while (true) {
        Person p;
        {
            unique_lock<mutex> lock(g_inputMutex);
            g_inputCV.wait(lock, [] {
                return !g_inputQueue.empty() || g_simDone.load();
            });
            if (g_inputQueue.empty() && g_simDone.load()) break;
            p = g_inputQueue.front();
            g_inputQueue.pop();
        }

        {
            unique_lock<mutex> cacheLock(g_cacheMutex);
            if (duration<double>(steady_clock::now() - lastCacheTime).count() > 0.1) {
                for (const string &eid : g_elevatorIDs) {
                    elevatorStatusCache[eid] = getBody(sendRequest("GET", "/ElevatorStatus/" + eid, ""));
                }
                lastCacheTime = steady_clock::now();
            }
        }

        double bestScore = -1e18;
        string bestElevator = "";

        for (const string &eid : g_elevatorIDs) {
            string body;
            {
                unique_lock<mutex> cacheLock(g_cacheMutex);
                body = elevatorStatusCache.count(eid) ? elevatorStatusCache[eid] : getBody(sendRequest("GET", "/ElevatorStatus/" + eid, ""));
            }
            if (body.empty() || body.find("DNE") != string::npos) continue;

            stringstream ess(body);
            string bayID, curFloorStr, direction, passengerCountStr, remainingCapStr;
            getline(ess, bayID, '|');
            getline(ess, curFloorStr, '|');
            getline(ess, direction, '|');
            getline(ess, passengerCountStr, '|');
            getline(ess, remainingCapStr, '|');

            auto trim = [](string &s) {
                s.erase(0, s.find_first_not_of(" \t\r\n"));
                s.erase(s.find_last_not_of(" \t\r\n") + 1);
            };
            trim(bayID);
            trim(curFloorStr);
            trim(direction);
            trim(passengerCountStr);
            trim(remainingCapStr);

            int curFloor = safeStoi(curFloorStr, -1);
            int remainingCap = safeStoi(remainingCapStr, 0);

            if (remainingCap <= 0) continue;

            // Check floor range from building file
            ElevatorInfo* info = nullptr;
            for (auto &ei : g_elevatorInfo) {
                if (ei.id == eid) { info = &ei; break; }
            }
            if (info) {
                if (p.startFloor < info->lowestFloor || p.startFloor > info->highestFloor) continue;
                if (p.endFloor < info->lowestFloor || p.endFloor > info->highestFloor) continue;
            }

            double distanceToPassenger = abs(curFloor - p.startFloor);
            double tripDistance = abs(p.startFloor - p.endFloor);

            // SRT - pick elevator with shortest remaining service time
            double score = -(distanceToPassenger + tripDistance);

            if (score > bestScore) {
                bestScore = score;
                bestElevator = eid;
            }
        }

        if (bestElevator.empty()) {
            int maxCap = -1;
            for (const string &eid : g_elevatorIDs) {
                string body;
                {
                    unique_lock<mutex> cacheLock(g_cacheMutex);
                    body = elevatorStatusCache.count(eid) ? elevatorStatusCache[eid] : getBody(sendRequest("GET", "/ElevatorStatus/" + eid, ""));
                }
                if (body.empty() || body.find("DNE") != string::npos) continue;

                stringstream ess2(body);
                string b1, b2, b3, b4, capStr;
                getline(ess2, b1, '|');
                getline(ess2, b2, '|');
                getline(ess2, b3, '|');
                getline(ess2, b4, '|');
                getline(ess2, capStr, '|');

                auto trim2 = [](string &s) {
                    s.erase(0, s.find_first_not_of(" \t\r\n"));
                    s.erase(s.find_last_not_of(" \t\r\n") + 1);
                };
                trim2(capStr);

                int cap = safeStoi(capStr, 0);

                ElevatorInfo* info = nullptr;
                for (auto &ei : g_elevatorInfo) {
                    if (ei.id == eid) { info = &ei; break; }
                }
                if (info) {
                    if (p.startFloor < info->lowestFloor || p.startFloor > info->highestFloor) continue;
                    if (p.endFloor < info->lowestFloor || p.endFloor > info->highestFloor) continue;
                }

                if (cap > maxCap) {
                    maxCap = cap;
                    bestElevator = eid;
                }
            }
        }

        if (bestElevator.empty()) continue;

        {
            unique_lock<mutex> lock(g_outputMutex);
            g_outputQueue.push({p.id, bestElevator});
        }
        g_outputCV.notify_one();
    }

    g_outputCV.notify_all();
}

/*
 * outputThread function: Sends elevator assignments to the API.
 */
void outputThread()
{
    while (true) {
        Assignment a;
        {
            unique_lock<mutex> lock(g_outputMutex);
            g_outputCV.wait(lock, [] {
                return !g_outputQueue.empty() || (g_simDone.load() && g_inputQueue.empty());
            });
            if (g_outputQueue.empty() && g_simDone.load() && g_inputQueue.empty()) break;

            a = g_outputQueue.front();
            g_outputQueue.pop();
        }

        string response = getBody(sendRequest("PUT", "/AddPersonToElevator/" + a.personID + "/" + a.elevatorID, ""));
        auto completionTime = steady_clock::now();

        if (response.find("added") != string::npos) {
            cout << "SUCCESS: Person " << a.personID << " assigned to elevator " << a.elevatorID
                 << " | Completed at: " << duration_cast<milliseconds>(completionTime.time_since_epoch()).count() << "ms" << endl;
        } else {
            cout << "FAILED: Person " << a.personID << " could not be assigned to elevator " << a.elevatorID
                 << " | Response: " << response << endl;
        }
    }
}

/*
 * main function: Entry point. Reads building file, starts simulation, launches threads.
 */
int main(int argc, char *argv[]) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <building_file> <port_number>\n";
        return 1;
    }

    try { g_port = stoi(argv[2]); }
    catch (...) {
        cerr << "Error: Invalid port number '" << argv[2] << "'\n";
        return 1;
    }
    if (g_port <= 0 || g_port > 65535) {
        cerr << "Error: Port must be between 1 and 65535.\n";
        return 1;
    }

    ifstream buildingFile(argv[1]);
    if (!buildingFile.is_open()) {
        cerr << "Error: Cannot open building file '" << argv[1] << "'\n";
        return 1;
    }

    string line;
    while (getline(buildingFile, line)) {
        if (line.empty()) continue;
        stringstream ls(line);
        string eid, low, high, cur, cap;
        getline(ls, eid, '\t');
        getline(ls, low, '\t');
        getline(ls, high, '\t');
        getline(ls, cur, '\t');
        getline(ls, cap, '\t');
        if (!eid.empty()) {
            g_elevatorIDs.push_back(eid);
            g_elevatorInfo.push_back({eid, safeStoi(low), safeStoi(high)});
        }
    }

    if (g_elevatorIDs.empty()) {
        cerr << "Error: No elevator IDs found in building file.\n";
        return 1;
    }

    sendRequest("PUT", "/Simulation/start", "");

    thread tInput(inputThread);
    thread tScheduler(schedulerThread);
    thread tOutput(outputThread);

    tInput.join();
    tScheduler.join();
    tOutput.join();

    return 0;
}
