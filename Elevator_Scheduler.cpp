/*
================================================================================
Title        : Elevator Scheduler.cpp
Description  : Elevator Scheduler for working with Elevator_OS by 
             : Eric Rees for CS4352 final project.
Authors      : Triston Schwab (R#11940154), Caleb Brasuell (R#11984197)
             : Matthew Cabrera (R#), Triston Barrientos (R#)
Date         : 4/27/2026
Version      : 0.3
Usage        : 
Notes        : Requires available port, 127.0.0.1:<port> to work
             : Requres use if Unix or Linux system for socket programming.
             : Alppys 3 threads to handle the scheduling of the elevators, and the communication with the API.
             : Using FIFO scheduling, the first person in the queue will be assigned to the first elevator that can service their request.
             : Does not hard code a port value. 
C++ Version  : C++ 17 
================================================================================
*/

//Standard CPP libraries
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cstring>
#include <fstream>
#include <pthread.h>

//Unix and Linux libraries for creating the network
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
using namespace std;


static int g_port; //Stores a port number

/*
 * sendRequest function: Handles all communication 
 * with the API. Sends requests to the API by creating the socket
 * and connecting to the server, if it fails simply returns an empty string.
 */

string sendRequest(const string &method, const string &path,
const string &sid, const string &body) {
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
    if (!sid.empty()) req << "X-Session: " << sid << "\r\n";
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
* selectElevator: Takes elevator ids read from 
* the building file from main.
*/
string selectElevator(int startFloor, int endFloor, const vector<string>& elevatorIDs) {
    
    for (const string& eid : elevatorIDs) 
    {
        string resp = sendRequest("GET", "/ElevatorStatus/" + eid, "", "");
        string body = getBody(resp);
        if (body == "DNE" || body.empty())
        {
            continue;
        }

        int lowest = stoi(parseField(body, "lowest"));
        int highest = stoi(parseField(body, "highest"));

        if ((startFloor >= lowest && startFloor <= highest) &&
            (endFloor >= lowest && endFloor <= highest))
        {
            return eid;
        }
    }
    //No valid elevator exists
    return "";
    
}

/*
 * main function: 
 */
int main(int argc, char *argv[]) {
    if (argc < 3) return 1;
    ifstream buildingFile(argv[1]);
    g_port = stoi(argv[2]); //Stores the port number for the session
    vector<string> elevatorIDs;
    string line;

    while (getline(buildingFile, line)) 
    {
        if (line.empty())
        {
            continue;
        }

        istringstream iss(line);
        string bay;
        iss >> bay;
        elevatorIDs.push_back(bay);
    }


    sendRequest("PUT", "/Simulation/start", "", "");

    //Temporary stand in scheduling loop
    while (true)
    {
        string statusResp = sendRequest("GET", "/Simulation/status", "", "");
        string statusBody = getBody(statusResp);

        //Wait a period if there is an empty status response
        if (statusBody.empty()) {
            cerr << "Empty status response \n";
            usleep(500000);
            continue;
        }

        if (statusBody.find("complete") != string::npos) 
        {
            break;
        }

        // Get next person in queue
        string personResp = sendRequest("GET", "/NextInput", "", "");
        string personBody = getBody(personResp);

        if (personBody == "NONE" || personBody.empty()) {
            // Nothing in queue, wait a moment and try again
            usleep(500000);
            continue;
        }

        // Parse person data
        string personID = parseField(personBody, "id");
        string startFloor = parseField(personBody, "startFloor");
        string endFloor = parseField(personBody, "endFloor");

        if (startFloor.empty() || endFloor.empty())
        {
            continue;
        }

        // Find a valid elevator 
        string chosenElevator = selectElevator(stoi(startFloor), stoi(endFloor), elevatorIDs);

        if (!chosenElevator.empty()) {
            sendRequest("PUT", "/AddPersonToElevator/" + personID + 
                "/" + chosenElevator, "", "");
        }

    }
    return 0;
}
