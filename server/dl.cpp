/*
 *created by dongqing xiao 
 *finished @14/4/2013 
 */

#include "header.h"
#include <sys/time.h>
#include <math.h>
#include <vector>
using namespace std;

/*constant variales */

#define BUFFER_SIZE 128
#define MAX_SEQ 4
#define MAX_PKT 200
#define TIMEOUT_MAX 8000000 

#define PHY 1
#define APP 2
#define TIME_OUT 3


/*frame structure
 * type : 0 for data,1 for ack
 * seq_num: seq num
 * data : store information
 */

typedef struct{
	int type; //0 for non ACK, 1 for ACK
	int seq_num;
	char *data;
} frame;




/*global variables*/



queue<string> phy_send_q[20];
queue<string> phy_receive_q[20];
queue<string> dl_send_q[20];
queue<string> dl_receive_q[20];
queue<string> app_send_q[20];
queue<string> app_receive_q[20];
queue<string> window_q[20];

pthread_mutex_t mutex_phy_send[20] = {PTHREAD_MUTEX_INITIALIZER};
pthread_mutex_t mutex_phy_receive[20] = {PTHREAD_MUTEX_INITIALIZER};
pthread_mutex_t mutex_dl_send[20] = {PTHREAD_MUTEX_INITIALIZER};
pthread_mutex_t mutex_dl_receive[20] = {PTHREAD_MUTEX_INITIALIZER};
pthread_mutex_t mutex_socket[20] = {PTHREAD_MUTEX_INITIALIZER};
pthread_mutex_t mutex_app_send_q[20] = {PTHREAD_MUTEX_INITIALIZER};
pthread_mutex_t mutex_app_receive_q[20] = {PTHREAD_MUTEX_INITIALIZER};
pthread_mutex_t mutex_window_q[20] = {PTHREAD_MUTEX_INITIALIZER};

/*initation*/
long timers[4]={0};
int queued = 0;
int k;
int previous_frame_received[20]={3};


long current_time();
int timeouts(void);
int wait_for_event(int client);
static void send_data(int client,int type,int seq_num,string data);
frame string_to_frame(string input);
int message_cutter(int client);
void frame_to_message(int client,char * inputdata, string &recv_temp_buff);

/*supporting function for usage*/

vector <string> split2(string src,string pattern){
    string::size_type start=0,index;
    string str=src;
    string substring;
    vector<string> result;
    
    do{
        index=str.find_first_of(pattern,start);
        if(index!=string::npos){
            substring=str.substr(start,index-start);
            result.push_back(substring);
            start=str.find_first_not_of(pattern,index);
            if(start==string::npos) break;
        }
    }while(index!=string::npos);
    
    // the last token
    substring=str.substr(start,str.size()-start);
    result.push_back(substring);
    return result;
}



long current_time(){
    struct timeval tv;
    struct timezone tz;
    struct tm *tm;
    gettimeofday(&tv,&tz);
    tm=localtime(&tv.tv_sec);
	long total=(tm->tm_min*100000000+tm->tm_sec*1000000+tv.tv_usec);
	return total;
}

int timeouts(void){
    
	long current=current_time();

	for (int i=0;i<queued;i++)
		if ((current-timers[i])>TIMEOUT_MAX){
			verbose("ERROR: Timeout occured (DL)");
			return 1;
		}
	return 0;
    
}

int message_cutter(int client){
    
    int E=dl_send_q[client].size();
    string message;
    string piece;
    
	for (int k=0;k<E;k++){
        message.clear();
        message=dl_send_q[client].front();
        dl_send_q[client].pop();
        int number_of_pieces=(int)ceil((double)message.size()/((double)BUFFER_SIZE+1));
        if (number_of_pieces>1)
            verbose("Message being cut into Pieces");
        
        //Add Delimiters
        for (int i=0;i<number_of_pieces;i++){
            piece.clear();
            if (i==(number_of_pieces-1)){//Last piece
                
                //Message has already been processed
                if ((message.find("\t")<256)||(message.find("\x88")<256)){
                    dl_send_q[client].push(message);
                }
                //Fresh Piece
                else{
                    piece=message.substr(i*(BUFFER_SIZE-1),i*BUFFER_SIZE+1+(message.size()%(BUFFER_SIZE+1)));
                    piece.append("\t");//end marker
                    dl_send_q[client].push(piece);
                }
            }
            else{
                string str=message.substr(0,BUFFER_SIZE-1);
                //First Piece
                dl_send_q[client].push(str.append("\x88"));//Mid message marker
                
            }
        }
        
    }
    
    return 0;
}



int wait_for_event(int client){
    
    int event=0;
	int client_temp;
	while(event<1){
	    client_temp=client;
	    pthread_mutex_lock( &mutex_phy_receive[client] );
	    if (!phy_receive_q[client].empty()){
            event=1;
	    	pthread_mutex_unlock( &mutex_phy_receive[client] );
            break;
	    }
	    else{
	    	pthread_mutex_unlock( &mutex_phy_receive[client] );
	    }
	    pthread_mutex_lock( &mutex_dl_send[client] );
	    if (!dl_send_q[client].empty()&&(queued!=4)){
            //Something to send for APP
            event=2;
            verbose("prepare to send message");
            message_cutter(client);
            
	    	pthread_mutex_unlock( &mutex_dl_send[client] );
            break;
	    }
	    else{
	    	pthread_mutex_unlock( &mutex_dl_send[client] );
	    }
	    if (timeouts()){//Need a timeout function
            event=3;
	    }
	    else{
           
	    }
	}
	
	return event;
}

/*sending data: transform frame into string (add separators)*/
static void send_data(int client,int type,int seq_num,string data){
    char type_c[20];
    char seq_num_c[20];
    sprintf(type_c,"%d",type);
    sprintf(seq_num_c,"%d",seq_num);
    
    
    string to_send=string(type_c)+'\a'+string(seq_num_c)+'\a'+data;
    
    /*push into the phy_send_q queue!*/
    
    pthread_mutex_lock(&mutex_phy_send[client]);
	phy_send_q[client].push(to_send);
	pthread_mutex_unlock(&mutex_phy_send[client]);
}

/**/

frame string_to_frame(string input){
    frame rec;
    vector <string> frame_string;
    frame_string=split2(input,"\a");
    rec.type=atoi(frame_string[0].c_str());
    rec.seq_num=atoi(frame_string[1].c_str());
    
   /*
    if(frame_string.size()==3)
    {
        char * tempdata=new char[frame_string[2].length()+1];
        strcpy(tempdata,frame_string[2].c_str());
    }
    else{
        string tempdata_s;
        tempdata_s.clear();
       
        for(int j=2;j<frame_string.size();i++){
            tempdata_s.append(frame_string[j]);
        }
        char * tempdata=new char[tempdata_s.length()+1];
        strcpy(tempdata,tempdata_s.c_str());
        
    }
    */
    char * tempdata=new char[frame_string[2].length()+1];
    strcpy(tempdata,frame_string[2].c_str());
    rec.data=tempdata;
    
    return rec;
}



/*frame into message, based on delimiter information (end mark/middle markers) */
void frame_to_message(int client,char* inputdata,string & recv_temp_buff)
{
    
    if(inputdata[strlen(inputdata)-1]=='\x88'){
        string temp=string(inputdata);
        recv_temp_buff.append(temp.substr(0,temp.length()-1));
    }
    else{
        recv_temp_buff.append(inputdata);
        
        for (int u=0;u<recv_temp_buff.size();u++)
            if (recv_temp_buff[u]=='\t'){
                //End of cutup message found
                string str2 = recv_temp_buff.substr (0,recv_temp_buff.length()-1);
                dl_receive_q[client].push(str2);
                recv_temp_buff.clear();
            }
        
    }
    
}


/*data link layer master*/
void *dl_layer_server(void  *client_num)
{
    int *client_temp=(int *) client_num;
    int const client = *client_temp;
    
    //pthread_mutex_t mutex_prev_seq_num = PTHREAD_MUTEX_INITIALIZER;
    
    int seq_num=0;
    int frame_expected=0;
    //int ack_expected = 0;
	int old_queued=0;
	//int rc;
	frame buffer;
	string recv_temp_buff;
	string data;
	string input;
	
	previous_frame_received[client]=3;
    
    while(1){
        int event=wait_for_event(client);
        switch(event){
            case(PHY):
                pthread_mutex_lock(&mutex_phy_receive[client]);
                input = phy_receive_q[client].front();
				pthread_mutex_unlock(&mutex_phy_receive[client]);
                /*fetch input from phy_receive_q*/
                buffer=string_to_frame(input);
                
                if(buffer.type==1)
                {
                    int start=frame_expected;
                    int count=0;
                    while(1){//Determine if acks need to be readjusted
						if (start==buffer.seq_num)
							break;
						start=(start+1)%4;
						count++;
					}
                    
                    /*stop timers, and clear window_q*/
                    for(int h=0;h<=count;h++){
						pthread_mutex_lock(&mutex_window_q[client]);
						if (!window_q[client].empty())
							window_q[client].pop();
						pthread_mutex_unlock(&mutex_window_q[client]);
						verbose("Queue Reduced in size (DL)");
						queued--;
						frame_expected=((frame_expected+1)%4);
					}
                    /*remove the data from phy_receive*/
					pthread_mutex_lock(&mutex_phy_receive[client]);
					phy_receive_q[client].pop();
					pthread_mutex_unlock(&mutex_phy_receive[client]);
                    
             

                }
                else
                {
                    /*...............*/
                    /*receive data frame*/
                    
                    if(buffer.seq_num==(previous_frame_received[client]+1)%4)
                    {
                        
                        previous_frame_received[client]=(previous_frame_received[client]+1)%4;
                        pthread_mutex_lock(&mutex_phy_receive[client]);
                        /*construct the frame from  */
                        
                        
                        frame_to_message(client,buffer.data,recv_temp_buff);
                        
                        phy_receive_q[client].pop();
                        pthread_mutex_unlock(&mutex_phy_receive[client]);
                        
                        send_data(client,1,buffer.seq_num,"ACK");
                      
                    }
                    else
                    {
                        /*drop the packet*/
                        verbose("drop the packet");
                        pthread_mutex_lock(&mutex_phy_receive[client]);
                        phy_receive_q[client].pop();
                        pthread_mutex_unlock(&mutex_phy_receive[client]);
                        
                        if(buffer.seq_num==previous_frame_received[client]){
                            verbose("duplicate packet(phy)");
                            send_data(client, 1, buffer.seq_num, "ACK");
                           
                        }
                        
                        
                        break;
                        
                        
                    }

                
                
                
                break;
            case(APP):
                    
                    verbose("deal with incoming message (APP)");
                    
                    if(queued>=MAX_SEQ)
                    {
                        if(old_queued!=queued){
                            verbose("full queue,waiting");
                            old_queued=queued;
                        }
                        break;
                    }
                    
                    
                    /*go here only spare space*/
                    
                    pthread_mutex_lock(&mutex_dl_send[client]);
                    //cout<<"get the lock"<<endl;
                    data.clear();
                    data=dl_send_q[client].front();
                    //cout<<"data1"<<data<<endl;
                    dl_send_q[client].pop();
                    
                    pthread_mutex_unlock(&mutex_dl_send[client]);
                    // get the data
                    
                    pthread_mutex_lock(&mutex_window_q[client]);
                    window_q[client].push(data);
                    pthread_mutex_unlock(&mutex_window_q[client]);
                    //push it into window_q
                    
                    timers[queued]=current_time();/*start timer*/
                    queued++;
                    
                    send_data(client,0,seq_num,data);
                    
                    seq_num=((seq_num+1)%4);
                    
                break;
            case(TIME_OUT):
                if(queued==0){
                    cout<<"no frame in queue while timeout"<<endl;
                    exit(1);
                }
                int start=0;
                start=(seq_num+4-queued)%4;
                
                /*retransmission*/
                for(int i=0;i<queued;i++){
                    
                    pthread_mutex_lock(&mutex_window_q[client]);
                
                    if(!window_q[client].empty()){
                        data=window_q[client].front();
                        window_q[client].push(window_q[client].front());
                        window_q[client].pop();
                    }
                    pthread_mutex_unlock(&mutex_window_q[client]);
                    
                    send_data(client,0,(start+i)%4,data);
                    timers[i]=current_time();
                }
                
                break;
        }
    }
    
    }
}
