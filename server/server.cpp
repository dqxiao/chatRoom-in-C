//MININET Server Application Layer
//TEAM 5

#include "header.h"
#include <math.h> // for ceil, floor

//max packet size
#define MAX_BUFF 192

//max packet size for FILE
#define maxFilePacketSize 100

//server port
int PORT;
int vb_mode = 0;

//file write control
int writing = 0;
//int probability=0;
int probability=0;

//packet 2-byte delimiter
string delim = "\x03\x03";

//File info structure
typedef struct {
	string filename;
	int clientId;
} fileInfo;
list<string> fileList;

//database record structure
struct dbRecord {
	int clientId;
	string username;
};

list<dbRecord> database; //User database

//receiving file packet queue
queue<string> file_recv_q[20];

//mutual exlusive lock for file receiving
pthread_mutex_t mutex_file_recv = PTHREAD_MUTEX_INITIALIZER;
//mutual exlusive lock for file writing
pthread_mutex_t mutex_writing = PTHREAD_MUTEX_INITIALIZER;

string convertIntString(int number);
string serv_getFromDL(const int clientId);
string serv_getUserlist(const int clientId);
string serv_getUsername(const int ID);
int serv_getClientID (const string name);
int rmvRecord(const int id);
void serv_splitSend(string message);
void serv_splitSend(const int clientId, string message);
void serv_sendAll(string sendMsg);
void serv_sendClient(const int clientId, string sendMsg);
void *serv_receiveFile(void* info);
void *serv_sendFile(void* info);
void serv_handleClient(int clientId);
bool serv_usernameExists(const string name);
bool fileExists(const char *filename);
bool addRecord(const int id, const string &username);


using namespace std;

int main(int argc, char *argv[]) {

	PORT = 5000;
	// initialize Physical Layer thread
	pthread_t phy_thread;
	int rc = pthread_create(&phy_thread, NULL, phy_layer_server, (void *) 1);
	if (rc) {
		cout << "Physical Layer Thread Failed to be created" << endl;
		exit(1);
	}

	// infinite loop to handle connected clients
	for (;;) {
		for (int i = 0; i < clients; i++) {
			pthread_mutex_lock(&mutex_dl_receive[i]);
			if (!dl_receive_q[i].empty()) {
				pthread_mutex_unlock(&mutex_dl_receive[i]);
				cout << "Handling client..." << endl;
				serv_handleClient(i);
			} else
				pthread_mutex_unlock(&mutex_dl_receive[i]);
		}
	}
}


// handle data coming from Data Link Layer
string serv_getFromDL(const int clientId) {
	string message = "";
	for (;;) {
		string temp = "";
//		pthread_mutex_lock(&mutex_dl_receive[clientId]);
		if (!dl_receive_q[clientId].empty())
			temp = dl_receive_q[clientId].front();
		else {
//			pthread_mutex_unlock(&mutex_dl_receive[clientId]);
			continue;
		}
		dl_receive_q[clientId].pop();
//		pthread_mutex_unlock(&mutex_dl_receive[clientId]);

		// find the delimiter and erase it from the packet
		if (temp.find(delim)) {
			temp.erase (temp.find(delim), delim.size());
			message.append(temp);
			return message; // return a meaningful packet
		}

		// concatenate if there is no delimiter
		else {
			message.append(temp);
		}
	}

//	if (str.find("FILE") == 0){
////		int fileLoc = str.find("FILE");
//		int lengLoc = 5;
//		int lengString = atoi (str.substr(lengLoc, 3).c_str());
//		int delimLoc = lengLoc + 4 + lengString;
//		str.erase (delimLoc, 1);
//	}
//	else if (str.find("\x03")){
//		str.erase(str.find('\x03'), 1);
//	}

	return 0;
}


// get username from client id
string serv_getUsername(const int clientId) {
	string name = "";
	for (list<dbRecord>::const_iterator it = database.begin();
			it != database.end(); it++) {
		if (it->clientId == clientId)
			name = it->username;
	}
	return name;
}

// get client id from username
int serv_getClientID (const string name){
	for (list<dbRecord>::iterator it = database.begin();
				it != database.end(); it++) {
			if (!(strcasecmp(it->username.c_str(), name.c_str()))) {
				return it->clientId;
			}
		}
	return -1;
}

// check to see whether an user exists in the db or not
bool serv_usernameExists(const string name) {
	for (list<dbRecord>::iterator it = database.begin();
			it != database.end(); it++) {
		if ((strcmp(it->username.c_str(), name.c_str())) == 0) {
			return true;
		}
	}
	return false;
}

// add an record to the db
bool addRecord(const int id, const string &username)
{
	if (serv_usernameExists(username)) //record existed
		return false;

	dbRecord rec = { id, username };
	database.push_back(rec);
	cout << username + " is added to the db" << endl;
	return true;
}


// remove a record from the db
int rmvRecord(const int id) {
	for (list<dbRecord>::iterator it = database.begin();
			it != database.end(); it++) {
		if (it->clientId == id) {
			cout << "Removed " << it->username << " from db" << endl;
			database.erase(it);
			return 1;
		}
	}
	return 0;
}

// get a list of currently available user
string serv_getUserlist(const int clientId) {
	string userList = "";
	for (list<dbRecord>::const_iterator it = database.begin();
			it != database.end(); it++) {
		if (userList.empty()) {
			userList = "Available users: " + it->username;
		} else
			userList = userList + ", " + it->username;
	}
	return userList;
}

// split a message into packets and broadcast
void serv_splitSend(string message) {

	string sendMsg = "";
	int pieces = (int)ceil((double) message.size() / ((double) MAX_BUFF + 1));

	for (int i = 0; i < pieces - 1; i++) {
		sendMsg.clear();
		sendMsg = message.substr(i * MAX_BUFF, MAX_BUFF);
		serv_sendAll(sendMsg);
	}
	sendMsg.clear();
	sendMsg = message.substr((pieces - 1) * MAX_BUFF,
			message.length() % (MAX_BUFF));
	serv_sendAll(sendMsg);
}

// split a message into packets and send to a specific user
void serv_splitSend(const int clientId, string message) {

	string sendMsg = "";
	int pieces = (int)ceil((double) message.size() / (double) MAX_BUFF);

	for (int i = 0; i < pieces - 1; i++) {
		sendMsg.clear();
		sendMsg = message.substr(i * MAX_BUFF, MAX_BUFF);
		serv_sendClient(clientId, sendMsg);
	}
	sendMsg.clear();
	sendMsg = message.substr((pieces - 1) * MAX_BUFF,
			message.length() % MAX_BUFF);
	serv_sendClient(clientId, sendMsg);
}

// broadcast a single message
void serv_sendAll(string sendMsg) {

	int cnt = 0;
	sendMsg.append (delim);
	for (list<dbRecord>::const_iterator it = database.begin();
			it != database.end(); it++) {
		cnt = it->clientId;
		pthread_mutex_lock(&mutex_dl_send[cnt]);
		dl_send_q[cnt].push(sendMsg);
		pthread_mutex_unlock(&mutex_dl_send[cnt]);
	}
	cout << "Send to all: " << sendMsg << endl;
}

// send a message to a specific client
void serv_sendClient(const int clientId, string sendMsg) {
	sendMsg.append (delim);
	pthread_mutex_lock(&mutex_dl_send[clientId]);
	dl_send_q[clientId].push(sendMsg);
	pthread_mutex_unlock(&mutex_dl_send[clientId]);

	cout << "Send to " << serv_getUsername(clientId)
			<< " message: " << sendMsg << endl;
}

// check whether a file exists or not
bool fileExists(const char *filename){
	ifstream ifile(filename);
	return ifile;
}

// Create 3-byte length header for each FILE packet
string convertIntString(int number)
{
	string add; // additional digits
	stringstream ss;
	if (number >= 0 && number < 10) // 1-digit number
		add  = "00";
	if (number >= 10 && number < 100) // 2-digit number
		add = "0";
	ss << add << number;//add number to the stream
	return ss.str();
}


// transfer a file to a client
void *serv_sendFile(void *info) {

	// get clientId and filename
	int clientId = (*((fileInfo*) (info))).clientId;
	string filename = (*((fileInfo*) (info))).filename.c_str();

	size_t size=0;

	// open file for reading
	ifstream file (filename.c_str(), ios::in|ios::binary|ios::ate);
	if (file.is_open()){
		size = file.tellg();
		char * memblock = new char [size + 1];
		file.seekg (0, ios::beg);
		file.read (memblock, size); //read file into a char array
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
			// handle the last packet
			else {
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
			pthread_mutex_lock(&mutex_dl_send[clientId]);
			dl_send_q[clientId].push(sendMsg);
			pthread_mutex_unlock(&mutex_dl_send[clientId]);
			packetIdx++;
		}
		// end-of-file packet
		// format: FEND <delimiter>
		sendMsg = "FEND";
		sendMsg.append(delim);
		pthread_mutex_lock(&mutex_dl_send[clientId]);
		dl_send_q[clientId].push(sendMsg);
		pthread_mutex_unlock(&mutex_dl_send[clientId]);
		cout << "Last file packet sent." << endl;
	}
	else
		cout << "Unable to open file.";
	return 0;
}

// receive file from a client
void *serv_receiveFile(void *info) {

	// get clientId and filename
	int clientId = (*((fileInfo*) (info))).clientId;
	string filename  = (*((fileInfo*) (info))).filename;

	// open file for writing
	ofstream file(filename.c_str(),ios::out | ios::binary);
	if (file.is_open()){
		string temp;
		for (;;) {
			// read file from the incoming queue
			// and write to the file
			pthread_mutex_lock(&mutex_file_recv);
			if (!file_recv_q[clientId].empty()) {
				temp.clear();
				temp = file_recv_q[clientId].front();

				file_recv_q[clientId].pop();
				file << temp.c_str();
			}
			pthread_mutex_unlock(&mutex_file_recv);

			// check if writing flag is still on
			// not throughoutly tested yet
			pthread_mutex_lock(&mutex_file_recv);
			pthread_mutex_lock(&mutex_writing);
			if (writing || !file_recv_q[clientId].empty()) {
				pthread_mutex_unlock(&mutex_writing);
				pthread_mutex_unlock(&mutex_file_recv);
				continue;
			} else {
				pthread_mutex_unlock(&mutex_writing);
				pthread_mutex_unlock(&mutex_file_recv);
				break;
			}
		}

		serv_sendAll(serv_getUsername(clientId) + " has uploaded " + filename);
		cout << "File received." << endl;
		file.close();
	}
	else
		cout << "Unable to open file.";
	return 0;
}

// handles individual client
void serv_handleClient(int clientId) {

	string command;
	string received = serv_getFromDL(clientId);
	istringstream iss(received);
	iss >> command;

	// handle login command
	if (!strcasecmp(command.c_str(), "login")) {
		string sendMsg;
		string user;
		iss >> user;

		// user existed
		if (serv_usernameExists(user)){
			cout << "Username already exists" << endl ;
			sendMsg = "USEREXISTEDERR";
		}
		else{
			cout << "Login approved." << endl;
			addRecord(clientId, user);
			sendMsg = "LOGGEDIN";
		}
		serv_sendClient(clientId, sendMsg);

	}

	// 'list' command received
	else if (!strcasecmp(command.c_str(), "list")) {
		cout << "'list' command received from " << serv_getUsername(clientId) << endl ;
		string userList = serv_getUserlist(clientId);
		cout << "--- Sending Userlist...\n" << userList << endl;
		if (userList.size() > MAX_BUFF - 1)
			serv_splitSend(clientId, userList);
		else
			serv_sendClient(clientId, userList);
	}

	// handle broastcast request
	else if (!strcasecmp(command.c_str(), "sendall")) {
		cout << "'sendall' command received from " << serv_getUsername(clientId) << endl ;
		string message = "";

		string temp;
		while (iss >> temp) {
			message = message + temp + " ";
		}
		message = message.substr(0,message.size() - 1);
		string user = serv_getUsername(clientId);
		string sendMsg = user + " said:" + message;

		//If message sent over MAX_BUFF, send to split, else send.
		if (sendMsg.size() > MAX_BUFF - 1)
			serv_splitSend(sendMsg);
		else
			serv_sendAll(sendMsg);
	}

	// send to a specified user
	else if (!strcasecmp(command.c_str(), "send")) {

		cout << "'send' command received from " << serv_getUsername(clientId) << endl ;
		string message = "";
		string otherUser;
		string sendMsg;
		string user;

		// get other user from header
		iss >> otherUser;
		// specified user doesn't exist
		if (serv_getClientID (otherUser) == -1){
			message = "ERROR: No such an user";
			serv_sendClient(clientId, message);
		}
		else{
			string temp;
			int otherUserID = serv_getClientID (otherUser);

			// construct message body
			while (iss >> temp) {
				message = message + temp + " ";
			}

			message = message.substr(0,message.size() - 1);
			user = serv_getUsername(clientId);
			sendMsg = user + " said:" + message;

			// handle too big message
			if (sendMsg.size() > MAX_BUFF - 1)
				serv_splitSend(otherUserID, sendMsg);
			else
				serv_sendClient(otherUserID, sendMsg);
		}
	}

	// handle quit request
	else if (!strcasecmp(command.c_str(), "quit")) {
		cout << "'quit' command received from " << serv_getUsername(clientId) << endl ;

		// empty queues
		pthread_mutex_lock(&mutex_dl_send[clientId]);
		dl_send_q[clientId] = std::queue<string>();
		pthread_mutex_unlock(&mutex_dl_send[clientId]);

		pthread_mutex_lock(&mutex_dl_receive[clientId]);
		dl_receive_q[clientId] = std::queue<string>();
		pthread_mutex_unlock(&mutex_dl_receive[clientId]);

		file_recv_q[clientId] = std::queue<string>();

		// send quit signal to the client and remove him from the db
		serv_sendClient(clientId, "QUIT");
		rmvRecord(clientId);
		cout << "Client terminated." << endl;
		return;
	}

	// handle file upload request
	else if (!strcasecmp(command.c_str(), "upload")) {
		cout << "'upload' command received from " << serv_getUsername(clientId) << endl ;
		pthread_mutex_lock(&mutex_writing);
		writing = 1;
		pthread_mutex_unlock(&mutex_writing);
		string fname;
		iss >> fname;
		fileInfo file;

		// construct file info
		file.filename = fname;
		file.clientId = clientId;

		fileList.push_front(file.filename);
		// create a new thread for receiving file
		pthread_t fileUpThread;
		int rc2 = pthread_create(&fileUpThread, NULL, serv_receiveFile, &file);
		if (rc2) {
			cout << "File receiving thread failed to be created" << endl;
			exit(1);
		}
	}

	// a file packet comes
	else if (!strcasecmp(received.substr(0, 4).c_str(), "FILE")) {

		//push the packet to the file receiving queue
		pthread_mutex_lock(&mutex_file_recv);
		file_recv_q[clientId].push(received.substr(9, received.length() - 9));
		pthread_mutex_unlock(&mutex_file_recv);
		return;
	}

	// end-of-file packet
	else if (!strcasecmp(received.substr(0, 4).c_str(),"FEND")) {
		cout << "Last packet received" << endl ;
		pthread_mutex_lock(&mutex_writing);
		writing = 0;
		pthread_mutex_unlock(&mutex_writing);
		serv_sendClient(clientId, "UPLOADDONE");
	}

	// handle download request
	else if (!strcasecmp(command.c_str(), "download")) {
		cout << "'download' command received from "
				<< serv_getUsername(clientId) << endl ;
		string filename;
		iss >> filename;

		pthread_t fileDownThread;
		fileInfo file;
		file.filename = filename;
		file.clientId = clientId;

		if (!fileExists (filename.c_str()))
			serv_sendClient(clientId, "FERR");

		else {
			// create a new thread for sending file
			int rc3 = pthread_create(&fileDownThread, NULL, serv_sendFile, &file);
			if (rc3) {
				cout << "File sending thread failed to be created" << endl;
				exit(1);
			}
		}
	}
	cout << "Finished handling " << serv_getUsername(clientId) << endl ;
	return;
}
