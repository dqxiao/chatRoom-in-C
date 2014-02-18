//MININET Client Application Layer
//TEAM 5

#include "header.h"
#include <math.h> // for ceil, floor
#include <iterator> //for std::istream_iterator

#define MAX_BUFF 192
#define maxFilePacketSize 100

//packet 2-byte delimiter
string delim = "\x03\x03";
int PORT; // server port
char * HOSTNAME; // server name
int vb_mode = 0;
int writing = 0; // file write flag
int probability = 0; // error introduction probability
int uploaddone = 0; // upload flag (done or not)
bool loggedIn = false; // login flag (logged in or not)
int userexisted = 0; // check user's existence in server
int nothingToAsk = 0; // debug


queue<string> file_recv_q;

//mutual exlusive lock for file receiving
pthread_mutex_t mutex_file_recv = PTHREAD_MUTEX_INITIALIZER;
//mutual exlusive lock for file writing
pthread_mutex_t mutex_writing = PTHREAD_MUTEX_INITIALIZER;

void *clnt_getFromDL(void *num);
void sendDL(string message);
string getFromDL(void);
bool fileExists(const char *filename);
string convertIntString(int number);
void clnt_sendFile(string filename);
void clnt_receiveFile(string filename);

using namespace std;

int main(int argc, char *argv[]) {

	PORT = 5000;
	int sockfd, n;
	struct sockaddr_in serv_addr;

	// check number of argument
	// requires exactly 2 arguments: hostname and error probability
	if (argc != 3){
		fprintf(stderr, "Invalid number of arguments");
		exit(0);
	}
	// 1st argument
	HOSTNAME = argv[1];
	// 2nd argument
	probability = atoi(argv[2]);

	string username;
	string buffer;

	// create a new thread for Data Link layer
	pthread_t dl_thread, recv_thread;
	int rc = pthread_create(&dl_thread, NULL, dl_layer_client, (void *) 1);
	if (rc) {
		cout << "Data Link Layer Thread Failed to be created" << endl;
		exit(1);
	}
	else {
		//create a new thread that listen to incoming Data Link queue
		int rc2 = pthread_create(&recv_thread, NULL, clnt_getFromDL,(void *) 1);
		if (rc2) {
			cout << "Receive thread failed to be created" << endl;
			exit(1);
		}
	}

	for(;;) { //infinite loop to accept and handle user input

		string sendString;
		cout << "Enter command: ";
		cout.flush();
		getline (cin, sendString);

		std::istringstream ss(sendString);
		std::istream_iterator<std::string> begin(ss), end;
		std::vector<std::string> inputArray(begin, end);
		/*number of elements in the token array*/
		int n_spaces = inputArray.size();

		// construct command from user input
		string command = inputArray[0];
		pthread_t recv_thread;

		// handle login command
		if (n_spaces == 2 && !strcasecmp(command.c_str(),"login")
				&&inputArray[1].size() <= 20 && !loggedIn){
//			cout << "Login block" << endl;
			username = inputArray[1];

			// message content
			buffer = "login " + username;

			//send command to DL Layer queue
			sendDL(buffer);
			for (;;){
				if (loggedIn || userexisted == 1)
					break;
			}

			// user already exists in the server
			if (userexisted == 1){
				cout << "User already existed." << endl;
				loggedIn = false;
				uploaddone = 0;
				continue;
			}

			loggedIn = true;
			userexisted = 0;
			uploaddone = 0;
			cout << "You are now logged in." << endl;
			continue;
		}

		string sendMsg;
		string filename;

		// logged in
		if (loggedIn) {
			buffer.clear();
			// handle quit command
			if (!strcasecmp(command.c_str(),"quit") && n_spaces == 1){
				sendMsg = command;

				// push command to DL layer as a packet
				sendDL(sendMsg);

				// wait for quit signal from server
				for (;;){
					if (loggedIn == false)
						break;
				}

				cout << "Quitted." << endl;

				// terminate all threads
				pthread_cancel(recv_thread);
				pthread_cancel(dl_thread);
				exit(0);
			}

			// handle list command
			else if (!strcasecmp(command.c_str(),"list") && n_spaces == 1){
				sendMsg = command;

				// push command to DL layer as a packet
				sendDL(sendMsg);
				sleep (1);
				sendMsg.clear();
				loggedIn = true;
			}

			// handle broadcast command
			else if (!strcasecmp(command.c_str(),"sendall") && n_spaces >= 2){
				std::istringstream iss(sendString);
				string sendCommand, input, message = "";
				iss >> sendCommand;

				// construct message content
				while (iss >> input) {
					message = message + input + " ";
				}
				message = message.substr(0,message.size() - 1);

				// message length must be smaller than 128 bytes
				if (message.size() <= 128){
					sendMsg = sendString;
					sendDL(sendMsg);
					sendMsg.clear();
					loggedIn = true;
					sleep (1);
				}
				else
					cout << "Message is too long...";
			}

			// send message to a specified user
			else if (!strcasecmp(command.c_str(),"send") && n_spaces >= 3){
				std::istringstream iss(sendString);
				string sendCommand, otherUser, input, message = "";
				iss >> sendCommand;
				// construct receiver
				iss >> otherUser;

				//construct message content
				while (iss >> input) {
					message = message + input + " ";
				}

				message = message.substr(0,message.size() - 1);
				cout << "Sending: " << message << " to user: " << otherUser << endl;

				// message lenght must be smaller than 128 bytes
				if (message.size() <= 128){
					sendMsg = sendString;
					sendDL(sendMsg);
					sendMsg.clear();
					loggedIn = true;
					sleep (1);
				}
				else
					cout << "Message is too long...";

			}

			// handle upload command
			else if (!strcasecmp(command.c_str(),"upload") && n_spaces == 2){

				// get filename and check its existence status
				filename = inputArray[1];
				if (fileExists(filename.c_str())){
					cout << "Upload file..." << endl;
					sendMsg = command + " " + filename;
					sendDL(sendMsg);
					sleep (1);

					// split file into packets and push them to DL layer queue
					clnt_sendFile(filename);

					// wait for upload done signal from server
					for (;;)
						if (uploaddone == 1)
							break;
					sleep (1);
					loggedIn = true;
					uploaddone = 0;
				}
				else{
					cout << "File open error" << endl;
					loggedIn = true;
				}
			}

			// handle download command
			else if (!strcasecmp(command.c_str(),"download") && n_spaces == 2){
				writing = 1; //enable writing

				// get download filename
				filename = inputArray[1];

				sendMsg = command + " " + filename;
				sendDL(sendMsg);
				sleep(1);

				// start receiving file from DL layer queue
				clnt_receiveFile(filename);
				loggedIn = true;
			}
			else{
				cout << "Invalid command\n" << endl;
				loggedIn = true;
			}
//			pthread_cancel(recv_thread);
		}
		else { //not logged in
			cout << "You are not logged in..." << endl;
			loggedIn = false;
		}
//		pthread_cancel(recv_thread);
	}
	return 0;
}

// send packet to DL layer
void sendDL(string message) {
	message.append(delim);
	pthread_mutex_lock(&mutex_dl_send);
	dl_send_q.push(message);
	pthread_mutex_unlock(&mutex_dl_send);
}


// handle data coming from Data Link Layer
string getFromDL(void) {
	string message = "";
	for (;;) {
		string temp = "";
		pthread_mutex_lock(&mutex_dl_receive);
		if (!dl_receive_q.empty()) {
			temp = dl_receive_q.front();
		} else {
			pthread_mutex_unlock(&mutex_dl_receive);
			continue;
		}
		dl_receive_q.pop();
		pthread_mutex_unlock(&mutex_dl_receive);

		// find the delimiter and erase it from the packet
		if (temp.find(delim)) {
			temp.erase (temp.find(delim), delim.size());
			message.append(temp);
			return message;
		}
		// concatenate if there is no delimiter
		else {
			message.append(temp);
		}
	}
//	cout << "---- BEFORE GET_STRING: " << str << endl <<endl;
//	if (str.find("FILE") == 0){
//		int fileLoc = str.find("FILE");
//		int lengLoc = fileLoc + 5;
//		int lengString = atoi (str.substr(lengLoc, 3).c_str());
//		int delimLoc = lengLoc + 4 + lengString;
//		str.erase (delimLoc, 1);
//	}
//	else if (str.find("\x03")){
//		str.erase(str.find('\x03'), 1);
//	}
////	cout << "---- AFTER GET_STRING: " << str << endl;

	return 0;
}

// handle server responses (thread)
void *clnt_getFromDL(void *num) {

	for (;;) {
		pthread_mutex_lock(&mutex_dl_receive);
		if (!dl_receive_q.empty()) {

			// gets data from queue
			pthread_mutex_unlock(&mutex_dl_receive);
			string recvd = getFromDL();

			// handle file packet
			if (!strcasecmp(recvd.substr(0, 4).c_str(),"FILE")) {
				pthread_mutex_lock(&mutex_file_recv);
//				int lengthString = atoi(recvd.substr(5,3).c_str());
				file_recv_q.push(recvd.substr(9, recvd.length() - 9));
				pthread_mutex_unlock(&mutex_file_recv);
			}

			// handle end-of-file packet
			else if (!strcasecmp(recvd.substr(0, 4).c_str(),"FEND"))
				writing = 0;

			// file does not exist in server
			else if (!strcasecmp(recvd.substr(0, 4).c_str(),"FERR")) {
				cout << "File does not exist, try again!" << endl;
				writing = 0;
			}

			// uploading is done
			else if (!strcasecmp(recvd.c_str(),"UPLOADDONE")){
				uploaddone = 1;
			}

			// server allow the user to log in
			else if (!strcasecmp(recvd.c_str(),"LOGGEDIN")){
				loggedIn = true;
			}

			// that user is already logged in
			else if (!strcasecmp(recvd.c_str(),"USEREXISTEDERR")){
				userexisted = 1;
				loggedIn = false;
			}

			// server allow to quit
			else if (!strcasecmp(recvd.c_str(),"QUIT")){
				loggedIn = false;
			}

			// display other messages from server
			else {
				cout << "\nMessage received from server:" << endl;
				cout << "'" + recvd + "'" << endl;
			}
		} else
			pthread_mutex_unlock(&mutex_dl_receive);
	}

	return 0;
}

// check whether a file exists or not
bool fileExists(const char *filename){
	ifstream ifile(filename);
	return ifile;
}

// create 3-byte length header for each FILE packet
string convertIntString(int number)
{
	string add;
	stringstream ss;//create a stringstream
	if (number >= 0 && number < 10)
		add  = "00";
	if (number >= 10 && number < 100)
		add = "0";
	ss << add << number;//add number to the stream
	return ss.str();//return a string with the contents of the stream
}


// transfer a file to server
// used by upload command
void clnt_sendFile(string filename) {

	size_t size=0;

	// open file for reading
	ifstream file (filename.c_str(), ios::in|ios::binary|ios::ate);
	if (file.is_open()){

		size = file.tellg();
		char * memblock = new char [size + 1];
		file.seekg (0, ios::beg);
		file.read (memblock, size);
		memblock[size]  = 0;
		file.close();

		// determine the number of packets the file is going to be splitted into
		int packetNum = (int) floor( (double)size / (double)maxFilePacketSize );
		// the size of the last packet
		int lastPacketSize = size % maxFilePacketSize;
		int fileIdx = 0;
		int packetIdx = 0;
		// keep splitting or not
		bool split = true;
		string sendMsg;
		string buffer;

		while (split) {
			// handle normal packets
			if (packetIdx < packetNum) {
				buffer.clear();
				for (int i = 0; i < maxFilePacketSize; i++) {
					buffer = buffer + memblock[fileIdx];
					fileIdx++;
				}
			}
			else {			// handle the last packet
				buffer.clear();
				for (int i = 0; i < lastPacketSize; i++) {
					buffer = buffer + memblock[fileIdx];
					fileIdx++;
				}
				split = false;
			}

			string lengthString = convertIntString(buffer.size());
			// packet format: FILE <packet length> <packet content><delimiter>
			sendMsg = "FILE " + lengthString + " " + buffer;
			sendMsg.append(delim);

			// push a packet into the queue for DL Layer
			pthread_mutex_lock(&mutex_dl_send);
			dl_send_q.push(sendMsg);
			pthread_mutex_unlock(&mutex_dl_send);
			packetIdx++;
		}
		// end-of-file packet
		// format: FEND <delimiter>
		sendMsg = "FEND";
		sendMsg.append(delim);
		pthread_mutex_lock(&mutex_dl_send);
		dl_send_q.push(sendMsg);
		pthread_mutex_unlock(&mutex_dl_send);
		cout << "Last file packet sent." << endl;
	}
	else
		cout << "Unable to open file.";
	return;
}

// receive file from server
// used by download command
void clnt_receiveFile(string filename) {

	// open file for writing
	ofstream file(filename.c_str(),ios::out | ios::binary);
	if (file.is_open()){
		string temp;
		for (;;) {
			// read file from the incoming queue
			// and write to the file
			pthread_mutex_lock(&mutex_file_recv);
			if (!file_recv_q.empty()) {
				temp.clear();
				temp = file_recv_q.front();

				file_recv_q.pop();
				file << temp.c_str();
			}
			pthread_mutex_unlock(&mutex_file_recv);

			pthread_mutex_lock(&mutex_file_recv);
			pthread_mutex_lock(&mutex_writing);

			// check if writing flag is still on
			// not throughoutly tested yet
			if (writing || !file_recv_q.empty()) {
				pthread_mutex_unlock(&mutex_writing);
				pthread_mutex_unlock(&mutex_file_recv);
				continue;
			} else {
				pthread_mutex_unlock(&mutex_writing);
				pthread_mutex_unlock(&mutex_file_recv);
				break;
			}
		}

		cout << "File received." << endl;
		file.close();
	}
	else
		cout << "Unable to open file.";
	return;
}

// kill the program and display error
void diewithError(string message) {
	cout << message << endl;
	exit(1);
}
