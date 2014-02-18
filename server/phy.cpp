/*
 */
#include "header.h"
#include <bitset>
#include <sys/signal.h>


#define BUFFER_SIZE 256

//Global variables 
char* HOSTNAME;
int clients=0;
pthread_t dl_thread[20];

//Function prototype
int get_crc(string str);
void diewithError(string message);
void verbose(string message);
string frame_to_bit(string input);
void deal_with_reading(string &input,int client);

string bytestuff(string input){
    
    string to_send;
    string begin="~";
    /*the represent of ox7e in string format*/
    string begin_stuff="~^";
    string middle_stuff="}3";
    to_send+=begin;
    
    
    for(int i=0;i<input.size();++i){
        if(input[i]=='}'){
            to_send+=middle_stuff;
        }
        else
            if(input[i]=='~'){
                to_send+=begin_stuff;
                
            }
            else {
                to_send+=input[i];
            }
    }
    
    to_send+=begin;
    
    
    return to_send;
}

string destuff(string input){
    string to_send;
    for (int i=0;i<input.size();++i){
        
        if((i==0)or(i==input.size()-1)){
            
        }
        else{
            if((input[i]=='~')and (input[i+1]=='^')){
                to_send+='~';
                i+=1;
            }
            else{
                if((input[i]=='}')and (input[i+1]=='3')){
                    to_send+='}';
                    i+=1;
                }
                else{
                    to_send+=input[i];
                }
            }
            
        }
        
    }
    
    return to_send;
}


typedef struct{
    int *socket;
    int client;
}info;



void verbose(string message){
	if (vb_mode) cout<<message<<endl;
}


string frame_to_bit(string input){
    string to_send;
    char crc_s[5];
    int crc=get_crc(input);
    sprintf(crc_s,"%d",crc);
    to_send=input;
    to_send.append(crc_s);
    /*stuff*/
    to_send=bytestuff(to_send);
    to_send.append("\b");
    return to_send;
}

int get_crc(string str){
    bitset<8> mybits=0;
    bitset<8> crc=0;
    for (int i=0;i<str.size();i++){
        
        mybits=bitset<8>(str[i]);
		crc=crc ^ mybits;
    }
    int g=(int) crc[0];
	return g;
}

void diewithError(string message) {
    cout<<message<<endl;
    exit(1);
}

void deal_with_reading(string &input,int client){
    char inbuff[255];
    strcpy(inbuff,input.c_str());
    input="";
    verbose("full message:"+string(inbuff)+"| (PHY)");
    string temp;
    
    for(int p=0;p<strlen(inbuff);p++){
        if(inbuff[p]=='\b'){
            char * pch;
            char crc_c[2];
            int crc;
            /*destuff*/
            verbose("before destuff"+temp);
            temp=destuff(temp);
            verbose("after destuff:"+temp);
            pch=new char [temp.size()+1];
            strcpy(pch,temp.c_str());
            crc_c[0]=pch[strlen(pch)-1];
            crc_c[1]='\0';
            crc=atoi(crc_c);
            /*get the crc in bit stream*/
            pch[strlen(pch)-1]='\0'; // build string
            if(get_crc(pch)==crc){
            }
            else{
                verbose("crc check failed(PHY)");
                verbose("corrupted message"+string(pch)+"|phy");
                pch=strtok(NULL,"\b");
                continue;
            }
            pthread_mutex_lock( &mutex_phy_receive[client] );
            phy_receive_q[client].push(pch);
            pthread_mutex_unlock( &mutex_phy_receive[client] );
            temp.clear();
            
        }
        else{
            temp=temp+inbuff[p];
        }
    }

}



/*implemetation*/

void *phy_layer_server(void *num){
    verbose("physical active (PHY)");
    
    int sockfd,portno;
    socklen_t clilen;
    void *newsockfd;
    struct sockaddr_in serv_addr, cli_addr;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        diewithError("ERROR opening socket (PHY)");
    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = PORT;
    
	//Basic Socket Parameters
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        diewithError("ERROR on binding (PHY)");
    listen(sockfd, 5);
    clilen = sizeof(cli_addr);
    
	//Threads
	int *socket[10];
    /*control thread, not so clevear*/
	pthread_t phy_layer_thread[10];
	info client_info[20];
	int client=0;
	int rc;
    try{
		while(1){
			//Wait for clients
			socket[client]=(int *) malloc(sizeof(int));
			verbose("Waiting for clients (PHY)");
			*socket[client]=accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
			verbose("Socket Accepted (PHY)");
            
            // Mark the socket as non-blocking, for safety.
			int x;
			x=fcntl(*socket[client],F_GETFL,0);
			fcntl(*socket[client],F_SETFL,x | O_NONBLOCK);
			if(*socket[client]==-1) diewithError("ERROR: Could not connect to client (PHY)");
			verbose("Socket Made non-blocking (PHY)");
			
			client_info[client].socket=socket[client];
			client_info[client].client=client;
			//Spawn Thread
			//cout<<&phy_layer_thread[client]<<endl;
			rc = pthread_create( &phy_layer_thread[client], NULL, phy_layer_t, &client_info[client]);
			if(rc)diewithError("ERROR; return code from pthread_create() (PHY)");
			pthread_detach(phy_layer_thread[client]);
			verbose("Thread spawned for client (PHY)");
			client++;
			clients=client;//Global
		}
	}
	//Something went wrong
	catch(int e) {
		//Stop threads
		for(int i=0;i<client;i++)
			pthread_cancel(phy_layer_thread[i]);
    }
	return 0;
    
}


void *phy_layer_t(void *num){
    info temp_info=*((info*)(num));
    int thefd;
    thefd=*temp_info.socket;
    
    int client=temp_info.client;
    fd_set read_flags,write_flags; // you know what these are
    struct timeval waitd;
    char outbuff[256];     // Buffer to hold outgoing data
    char inbuff[256];      // Buffer to read incoming data into
    int err;	       // holds return values
    string previous;
    string temp;
    
    memset(&outbuff,0,sizeof(outbuff)); // memset used for portability
    int crc;
    int total=0;
    int rc;
    rc = pthread_create(&dl_thread[client], NULL, dl_layer_server, &client);
    if (rc){
        diewithError("ERROR: Data Link Layer Thread Failed to be created (PHY)");
    }
    
    // want to send or receive message
    
    while(1){
        if(pthread_kill(dl_thread[client], 0)){
            verbose("ERROR: DL Thread died for client (PHY)");}
        
        FD_ZERO(&read_flags); // Zero the flags ready for using
        FD_ZERO(&write_flags);
        FD_SET(thefd, &read_flags);
        
    	memset(&outbuff,0,256); // memset used for portability
    	memset(&inbuff,0,256);
        
      
        pthread_mutex_lock( &mutex_phy_send[client] );
        if(!phy_send_q[client].empty()){
            FD_SET(thefd, &write_flags);
        }
        pthread_mutex_unlock( &mutex_phy_send[client] );
        
        err=select(thefd+1, &read_flags,&write_flags,(fd_set*)0,&waitd);
        if(err < 0) continue;
        // reading something
        if(FD_ISSET(thefd, &read_flags)){
            FD_CLR(thefd, &read_flags);
            memset(&inbuff,0,sizeof(inbuff));
            if (read(thefd, inbuff, sizeof(inbuff)-1) <= 0) {
                close(thefd);
                verbose("Socket Closed (PHY)");
                break;
            }
            previous.append(string(inbuff));
            /*need change if you switch to different kind stuffing*/
            if(previous[previous.length()-1]!='\b'){
               
                continue;
            }
            deal_with_reading(previous,client);
            
        }
        // write something
        if(FD_ISSET(thefd, &write_flags)){
            FD_CLR(thefd, &write_flags);
            temp.clear();
            pthread_mutex_lock( &mutex_phy_send[client] );
            temp=phy_send_q[client].front();
            pthread_mutex_unlock( &mutex_phy_send[client] );
            
            temp=frame_to_bit(temp);
            strcpy(outbuff,temp.c_str());
            
            //Send Message
            verbose("Sending (PHY): "+string(outbuff));
            pthread_mutex_lock( &mutex_phy_send[client] );
            phy_send_q[client].pop();
            pthread_mutex_unlock( &mutex_phy_send[client] );
            
            write(thefd,outbuff,strlen(outbuff));
            memset(&outbuff,0,sizeof(outbuff));
        }
        
    }
    
    close(thefd);
    
}
