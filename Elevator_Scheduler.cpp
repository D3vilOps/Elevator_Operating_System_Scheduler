/*
================================================================================
Title        : Elevator Scheduler.cpp
Description  : Elevator Scheduler for working with Elevator_OS by 
             : Eric Rees for CS4352 final project.
Authors      : Triston Schwab (R#11940154), Caleb Brasuell (R#11984197)
             : Matthew Cabrera (R#11802764), Triston Barrientos (R#11688728)
Date         : 4/27/2026
Version      : 1.0
Usage        : 
Notes        : Requires available port, 127.0.0.1:<port> to work
             : Requres use if Unix or Linux system for socket programming.
             : Alppys 3 threads to handle the scheduling of the elevators, and 
             : the communication with the API. 
             : Does not hard code a port value. Select elevator function absorbed by the 
             : scheduler thread. Uses SPN scheduling and HRRN for tie breakingh
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

//Unix and Linux libraries for creating the network
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;
using namespace chrono;


static int g_port = 0; //Stores a port number
static vector<string> g_elevatorIDs;

struct Person {
    string id;
    int startFloor;
    int endFloor;
    steady_clock::time_point arrivalTime;
};

struct Assignment {
    string personID;
    string elevatorID;
};

//Input for Scheduler Thread
queue<Person> g_inputQueue;
mutex g_inputMutex;
condition_variable g_inputCV;

//Output for Scheduler Thread
queue<Assignment> g_outputQueue;
mutex g_outputMutex;
condition_variable g_outputCV;

atomic<bool> g_simDone(false);

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
    auto pos = response.find("\r\n\r\n");
    if (pos == string::npos) return "";
    return response.substr(pos + 4);
}

/*
 * parseField function: Parses values by searching for value= and finds the end of the value
 * by searching for ";", "\r", or "\n" 
 * returns the value substring
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
* safeStoi function:
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
        string statusBody = getBody(sendRequest("GET", "/Simulation/status", ""));
        if (statusBody.find("complete") != string::npos) {
            g_simDone.store(true);
            g_inputCV.notify_all();
            g_outputCV.notify_all();
            break;
        }

        string personBody = getBody(sendRequest("GET", "/NextInput", ""));
        if (personBody.empty() || personBody == "NONE") { 
            this_thread::sleep_for(milliseconds(200));
            continue;
        }

        string idStr = parseField(personBody, "id");
        string startStr = parseField(personBody, "startFloor");
        string endStr = parseField(personBody, "endFloor");

        if (idStr.empty() || startStr.empty() || endStr.empty()) { 
            continue;
        }

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
* schedulerThread function:
*/
void schedulerThread() 
{
    while (true) {

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

            p = g_inputQueue.front();
            g_inputQueue.pop();
        }
        
        double waitTime = duration<double>(steady_clock::now() - p.arrivalTime).count();

        double bestRatio = -1.0;
        string bestElevator = "";

        for (const string &eid : g_elevatorIDs) {
            string body = getBody(sendRequest("GET", "/ElevatorStatus/" + eid, ""));
            if (body.empty() || body == "DNE") 
            {
                continue;
            }

            int lowest   = safeStoi(parseField(body, "lowest"),       999999);
            int highest  = safeStoi(parseField(body, "highest"),     -999999);
            int curFloor = safeStoi(parseField(body, "currentFloor"),     -1);
 
            if (p.startFloor < lowest  || p.startFloor > highest) continue;
            if (p.endFloor   < lowest  || p.endFloor   > highest) continue;
            if (curFloor < 0) continue;
 
            double serviceTime = abs(curFloor - p.startFloor)
                               + abs(p.startFloor - p.endFloor);
            if (serviceTime < 1.0) serviceTime = 1.0;
 
            double ratio = (waitTime + serviceTime) / serviceTime;
            if (ratio > bestRatio) {
                bestRatio    = ratio;
                bestElevator = eid;
            }
        }

        if (bestElevator.empty()) {
            // No elevator available yet — re-queue and retry
            {
                unique_lock<mutex> lock(g_inputMutex);
                g_inputQueue.push(p);
            }
            g_inputCV.notify_one();
            this_thread::sleep_for(milliseconds(100));
            continue;
        }
 
        {
            unique_lock<mutex> lock(g_outputMutex);
            g_outputQueue.push({p.id, bestElevator});
        }
        g_outputCV.notify_one();
    }
 
    g_outputCV.notify_all();
    
}

/*
*outputThread function:
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
 
        sendRequest("PUT", "/AddPersonToElevator/" + a.personID + "/" + a.elevatorID, "");
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
        //Look for the first whitespace to isolate the first token
        auto end = line.find("\t");
        string eid = (end == string::npos) ? line : line.substr(0, end);
        if (!eid.empty()) g_elevatorIDs.push_back(eid);
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
