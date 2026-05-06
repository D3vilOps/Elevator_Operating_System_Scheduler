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
             : Requres use if Unix or Linux system for socket programming.
             : Applies 3 threads to handle the scheduling of the elevators, and 
             : the communication with the API. 
             : Does not hard code a port value. Select elevator function absorbed by the 
             : scheduler thread. Uses SPN scheduling and HRRN for tie breaking
             : as well as FIFO for the last resort.
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
    string id;//Unique identifier for the person
    int startFloor;//Floor the person starts on
    int endFloor;//Floor the person wants to go to
    steady_clock::time_point arrivalTime; //Time the person was received by the input thread
};

struct Assignment {
    string personID; //Unique identifier for the person
    string elevatorID;//Unique identifier for the elevator
};

//Input for Scheduler Thread
queue<Person> g_inputQueue;//Queue to store incoming people from the API
mutex g_inputMutex; //Mutex to protect access to the input queue
condition_variable g_inputCV; //Condition variable to signal the scheduler thread when a new person is added to the input queue

//Output for Scheduler Thread
queue<Assignment> g_outputQueue; //Queue to store assignments of people to elevators for the output thread
mutex g_outputMutex; //Mutex to protect access to the output queue
condition_variable g_outputCV; //Condition variable to signal the output thread when a new assignment is added to the output queue

atomic<bool> g_simDone(false); //Atomic flag to indicate when the simulation is complete, used to signal threads to exit when done

/*
 * sendRequest function: Handles all communication 
 * with the API. Sends requests to the API by creating the socket
 * and connecting to the server, if it fails simply returns an empty string.
 */

string sendRequest(const string &method, const string &path, const string &body) 
{
    //Create the socket to connect to
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return "";

    //Set up the server's address
    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(g_port);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

    //Connect to the server itself, on fail return an empty string
    if (connect(sock, reinterpret_cast<sockaddr*>(&server), sizeof(server)) < 0) {
        close(sock);
        return "";
    }

    //Creating the HTTP request to the server
    ostringstream req;
    req << method << " " << path << " HTTP/1.0\r\n";
    req << "Host: 127.0.0.1:" << g_port << "\r\n";
    req << "Content-Type: text/plain\r\n";
    req << "Content-Length: " << body.size() << "\r\n";
    req << "\r\n";
    req << body;

    //Send HTTP request out
    string request = req.str();
    send(sock, request.c_str(), request.size(), 0);

    //Receive response
    string response;
    char buf[4096];
    int n;
    while ((n = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        response += buf;
    }
    close(sock);
    return response; //Returns full raw response
}

/*
 * getBody function: Finds and seperates out the body in the message
 * in event string::npos, where no found message was received, return an
 * empty string.
 */
string getBody(const string &response) {
    auto pos = response.find("\r\n\r\n"); //Find the end of the headers
    if (pos == string::npos) return ""; //No body found, return empty string
    return response.substr(pos + 4); //Return the body of the response
}

/*
 * parseField function: Parses values by searching for value= and finds the end of the value
 * by searching for ";", "\r", or "\n" 
 * returns the value substring
 */
string parseField(const string &body, const string &key) {
    string search = key + "="; //Search for the key followed by an equals sign
    auto pos = body.find(search); //Find the position of the key in the body
    if (pos == string::npos) return ""; //Key not found, return empty string
    pos += search.size(); //Move position to the start of the value
    auto end = body.find_first_of(";\r\n", pos); //Looking for the next ";" or newline
    if (end == string::npos) return body.substr(pos);//No end found, return the rest of the string
    return body.substr(pos, end - pos); //Return the value substring
}

/*
* safeStoi function: Safely converts a string to an integer, returning a default value if the string is empty or cannot be converted to an integer.
*/
int safeStoi(const string &s, int defaultVal = 0) 
{
    if (s.empty()) 
    {
        return defaultVal;
    }
    try 
    {
        return stoi(s);
    }
    catch(...) 
    {
        return defaultVal;
    }
}

/*
* inputThread function:
*/
void inputThread() 
{
    while (!g_simDone.load()) {
        //Check if simulation has completed
        string statusBody = getBody(sendRequest("GET", "/Simulation/check", "")); // Get the currect status of Simulation
        if (statusBody.find("complete") != string::npos) {
            g_simDone.store(true); // Set the simulation done flag to true
            g_inputCV.notify_all(); // Notify all threads waiting on the input condition variable
            g_outputCV.notify_all(); // Notify all threads waiting on the output condition variable
            break; // Exit the loop and end the thread
        }
        //Get the next person from the API
        string personBody = getBody(sendRequest("GET", "/NextInput", ""));
        if (personBody.empty() || personBody == "NONE") { 
            this_thread::sleep_for(milliseconds(50));
            continue;
        }

        stringstream ss(personBody);
        string idStr, startStr, endStr;
        getline(ss, idStr, '|');
        getline(ss, startStr, '|');
        getline(ss, endStr, '|');

        // Trim whitespace
        auto trim = [](string &s) {
            s.erase(0, s.find_first_not_of(" \t\r\n"));
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
        };
        trim(idStr);
        trim(startStr);
        trim(endStr);

        if (idStr.empty() || startStr.empty() || endStr.empty()) {
            continue;
        }
 
        //Create a Person struct and fill it with the data
        Person p;
        p.id = idStr;
        p.startFloor = safeStoi(startStr);
        p.endFloor = safeStoi(endStr);
        p.arrivalTime = steady_clock::now();
        //Add the person to the input queue and notify the scheduler thread
        {
            unique_lock<mutex> lock(g_inputMutex);
            g_inputQueue.push(p);
        }
        g_inputCV.notify_one();
    }
}

/*
* schedulerThread function: Schedules the elevators according to an SPN policy,
* in the case of a tiebreak, uses HRRN and FCFS as last resort.
* Uses the Person struct for a person's information.
*/
void schedulerThread() 
{
    //Loop until the simulation is done and there are no more people to process
    while (true) {
        //Get the next person from the input queue
        Person p;
        {
            unique_lock<mutex> lock(g_inputMutex);
            
            g_inputCV.wait(lock, []
            {
                return !g_inputQueue.empty() || g_simDone.load();
            });

            if (g_inputQueue.empty() && g_simDone.load())
            {
                break;
            }
            //Pop the person from the input queue
            p = g_inputQueue.front();
            g_inputQueue.pop();
        }
        
        //Calculate the wait time for this person
        double waitTime = duration<double>(steady_clock::now() - p.arrivalTime).count();

      
        double bestRatio = -1.0;
        string bestElevator = "";
        static unordered_map<string, string> elevatorStatusCache;
        static auto lastCacheTime = steady_clock::now();

        if (duration<double>(steady_clock::now() - lastCacheTime).count() > 0.1) {
            for (const string &eid : g_elevatorIDs) {
                elevatorStatusCache[eid] = getBody(sendRequest("GET", "/ElevatorStatus/" + eid, ""));
            }
            lastCacheTime = steady_clock::now();
        }

        for (const string &eid : g_elevatorIDs) {
            string body = elevatorStatusCache.count(eid) ? elevatorStatusCache[eid] : getBody(sendRequest("GET", "/ElevatorStatus/" + eid, ""));
            if (body.empty() || body == "DNE") 
            {
                continue;
            }

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
            double serviceTime = distanceToPassenger + tripDistance;
            if (serviceTime < 1.0) serviceTime = 1.0;

            // SRT - pick elevator with shortest remaining service time
            double score = -(distanceToPassenger + tripDistance);

            if (score > bestRatio) {
                bestRatio    = score;
                bestElevator = eid;
            }

        }

        if (bestElevator.empty()) {
            // Force assign to elevator with most remaining capacity
            int maxCap = -1;
            for (const string &eid : g_elevatorIDs) {
                string body = elevatorStatusCache.count(eid) ? elevatorStatusCache[eid] : getBody(sendRequest("GET", "/ElevatorStatus/" + eid, ""));
                if (body.empty() || body == "DNE") continue;
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
*outputThread function: Outputs the elevators assignments. 
* Uses the Assignment struct for its outputs.
*/
void outputThread()
{
    while (true) {
 
        Assignment a;
        {
            unique_lock<mutex> lock(g_outputMutex);
            g_outputCV.wait(lock, [] {
                return !g_outputQueue.empty() || g_simDone.load();
            });
            if (g_outputQueue.empty() && g_simDone.load()) break;
 
            a = g_outputQueue.front();
            g_outputQueue.pop();
        }
 
        string response = getBody(sendRequest("PUT", "/AddPersonToElevator/" + a.personID + "/" + a.elevatorID, ""));
        auto completionTime = steady_clock::now();

        if (response.find("added") != string::npos) {
            cout << "SUCCESS: Person " << a.personID << " assigned to elevator " << a.elevatorID << " | Completed at: " << duration_cast<milliseconds>(completionTime.time_since_epoch()).count() << "ms" << endl;
        } else {
            cout << "FAILED: Person " << a.personID << " could not be assigned to elevator " << a.elevatorID << " | Response: " << response << endl;
        }
    }
}


/*
 * main function: 
 */
int main(int argc, char *argv[]) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <building_file> <port_number>\n";
        return 1;
    }
 
    try   { g_port = stoi(argv[2]); }
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
 
    //Read one elevator per line
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
 
    // Launch the threads
    thread tInput(inputThread);
    thread tScheduler(schedulerThread);
    thread tOutput(outputThread);
 
    //Join the threads together
    tInput.join();
    tScheduler.join();
    tOutput.join();
 
    return 0;
}
