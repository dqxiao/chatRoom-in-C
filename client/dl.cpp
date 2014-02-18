/*
 * created by dongqing xiao 
 * finished on 14/4 2013 
 */
#include "header.h"
#include <sys/time.h>
#include <math.h>
#include <vector>

using namespace std;

/*
 *setting
 *MAX_SEQ :window size
 *MAX_PKT :frame data maximum length
 *Buffer_size: frame fraction size 
 *TIMEOUT_MAX: time out,1 sec=1000000, wait for 5 secs
 */
#define MAX_SEQ 4 
#define MAX_PKT 200
#define BUFFER_SIZE 128 
#define TIMEOUT_MAX 5000000 

/*event setting*/
#define PHY 1
#define APP 2
#define TIME_OUT 3

/*frame structure
 * type : 0 for data,1 for ack 
 * seq_num: seq num
 * data : store information
 */

typedef struct {
    int type;
    int seq_num;
    char * data;
}frame;

/*global variables*/
/*
 *timers[i]: record the time span for data frame in window_q
 *queued: the number of frames in the window 
 */
long timers[5]={0};
int queued=0;
int k;
string data;

/*
 * DL: fetch from dl_send_q,after fraction,push message waiting for sending into phy_send_q 
 *   : after re-assamble, receive frame from phy_receive_q
 *APP: after message division, push into dl_send_q 
 *   : fetch from dl_send_q, receive frame reconstruction to message , window_q 
 *phyical: fetch from phy_send_q to TCP/IP, then sending
 *       : receiving from TCP/IP, into phy_receive_q;
 */

queue<string> phy_send_q;
queue<string> phy_receive_q;
queue<string> dl_send_q;
queue<string> dl_receive_q;
queue<string> app_send_q;
queue<string> app_receive_q;
queue<string> window_q;

/*for multiple read/write control, keep concurrency!*/
pthread_mutex_t mutex_phy_send = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_phy_receive = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_dl_send = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_dl_receive = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_socket = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_app_send = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_app_receive = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_window_q = PTHREAD_MUTEX_INITIALIZER;



/*split function: return vector of words*/
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


/*protype functions*/
long current_time();
int timeout(void);
int wait_for_event(void);
int message_cutter(void);
int send_message(void);
static void send_data(int type,int seq_num,string data);
frame string_to_frame(string input);
void frame_to_message(char * inputdata, string &recv_temp_buff);

/*real implementation reference minet*/
/*time function*/
long current_time(){
    struct timeval tv;
    struct timezone tz;
    struct tm *tm;
    gettimeofday(&tv,&tz);
    tm=localtime(&tv.tv_sec);
	long total=(tm->tm_min*100000000+tm->tm_sec*1000000+tv.tv_usec);
	return total;
}

/*check whether it is time out or not*/
int timeout(void){
    long current=current_time();
    for(int i=0;i<queued;i++)
        if((current-timers[i])>TIMEOUT_MAX){
            return 1;
        }
    return 0; // no timeout;
}


int wait_for_event(void){
    int event=0;// noting happen
    while(event<1){
        if(!phy_receive_q.empty())
            event=1; //phy send
        else if((!dl_send_q.empty())&&(queued!=4)){
            event=2;
            message_cutter();
            //send_message();
            // when the queue is full, turn in to timeout very quickly
        }
        else if(timeout())
            event=3;
    }
    
    return event;
}

//dq_xiao


/*from string(physical layer) to frame (data linker layer)*/
frame string_to_frame(string input){
    frame rec;
    vector <string> frame_string;
    frame_string=split2(input,"\a");
    rec.type=atoi(frame_string[0].c_str());
    rec.seq_num=atoi(frame_string[1].c_str());
    
    char * tempdata=new char[frame_string[2].length()+1];
    strcpy(tempdata,frame_string[2].c_str());
    
    rec.data=tempdata;
    
    return rec;
}

/* from packet (appliation layer) to frame(data link layer) and push into queue*/
/* fetch from dl_send_q, push into dl_send_q*/

int send_message()
{
    
    pthread_mutex_lock(&mutex_dl_send);
	int E=dl_send_q.size();
	string message;
	string piece;
    
	for (int k=0;k<E;k++){
		message.clear();
		message=dl_send_q.front();
		dl_send_q.pop();
        
        if ((message[message.length()-1]=='\t')||(message[message.length()-1]=='\x88'))
        {
            dl_send_q.push(message);
            continue;
        }
		int number_of_pieces=(int)ceil((double)message.size()/((double)BUFFER_SIZE+1));
		if (number_of_pieces>2)
			verbose("Message being cut into Pieces");
		/*fresh pieces*/
        
		for (int i=0;i<number_of_pieces;i++)
        {
			piece.clear();
            
			if (i==(number_of_pieces-1))
            {
                piece=message.substr(i*(BUFFER_SIZE-1),i*BUFFER_SIZE+1+(message.size()%(BUFFER_SIZE+1)));
					piece.append("\t");//end marker
					dl_send_q.push(piece);
            }
			else{
				string str=message.substr(0,BUFFER_SIZE-1);
				dl_send_q.push(str.append("\x88")); // middle marker 
			}
		}
        
	pthread_mutex_unlock(&mutex_dl_send);
    
	return 0;

}
}

int message_cutter(){
    pthread_mutex_lock(&mutex_dl_send);
	int E=dl_send_q.size();
	string message;
	string piece;
    
	for (int k=0;k<E;k++){
		message.clear();
		message=dl_send_q.front();
		dl_send_q.pop();
        
		int number_of_pieces=(int)ceil((double)message.size()/((double)BUFFER_SIZE+1));
		if (number_of_pieces>100)
			verbose("Message being cut into Pieces");
		//Add Delimiters
		for (int i=0;i<number_of_pieces;i++){
			piece.clear();
			if (i==(number_of_pieces-1)){
                //Last piece
                
				//Message has already been processed
				if ((message[message.length()-1]=='\t')||(message[message.length()-1]=='\x88')){
					dl_send_q.push(message);
				}
                //Fresh Piece
				else{
					piece=message.substr(i*(BUFFER_SIZE-1),i*BUFFER_SIZE+1+(message.size()%(BUFFER_SIZE+1)));
					piece.append("\t");//add end marker
					dl_send_q.push(piece);
				}
			}
			else{
				string str=message.substr(0,BUFFER_SIZE-1);
				dl_send_q.push(str.append("\x88"));//Mid message marker
                
			}
		}
        
	}
	pthread_mutex_unlock(&mutex_dl_send);
    
	return 0;
}


    


static void send_data(int type,int seq_num,string data)
{
    char seq_num_c[20]; /*for turn int into string*/
    char type_c[20];
    sprintf(seq_num_c,"%d",seq_num);
    sprintf(type_c,"%d",type);

    string to_send=string(type_c)+'\a'+string(seq_num_c)+'\a'+data;
    // turn packet into string
    
    /* require the lock 
     * push into queue 
     * release the lock 
     */
    pthread_mutex_lock(&mutex_phy_send);
    phy_send_q.push(to_send);
    pthread_mutex_unlock(&mutex_phy_send);
}

/*construct message by assmebing frames, using end marker and middle makrker to this job*/
void frame_to_message(char* inputdata,string & recv_temp_buff){
    // append into recv_temp_buff until encounter the last cutup frame 
    if(inputdata[strlen(inputdata)-1]=='\x88'){
        string temp=string(inputdata);
        recv_temp_buff.append(temp.substr(0,temp.length()-1));
    }
    else{
        recv_temp_buff.append(inputdata);
        for (int u=0;u<recv_temp_buff.size();u++)
            if (recv_temp_buff[u]=='\t')
            {
                string str2 = recv_temp_buff.substr (0,recv_temp_buff.length()-1);
                dl_receive_q.push(str2);
                recv_temp_buff.clear();
            }
        
    }
   
}


/* dl_layer_client, enter point in client_cpp*/
void * dl_layer_client(void *num){
    //num for file description
    /*variable*/
   
    
    int send_seq_num=0;
    int ack_expected=0;
    int previous_frame_received=3;
    int rc;
    int old_queued=0;
    frame buffer;
    string recv_temp_buff;
    
    /* initlize physical layer, make connection*/
    pthread_t phy_thread;
	rc = pthread_create(&phy_thread, NULL, phy_layer_t, (void *) 1);
	if (rc){
		cout<<"Physical Layer Thread Failed to be created"<<endl;
		exit(1);
	}
    
    
    verbose("waiting for connection(dl)"); // for debugging
    // connected: general variable, changed by physcal layer set up
    while(connected)
        continue;
    
    while(1){
        int event=wait_for_event();
        switch(event)
        {
                
            case(PHY):
            
                /* received something from phyiscal layer
                 fetch from phy_receive ,then deal with it*/
                buffer=string_to_frame(phy_receive_q.front());
                
                
                if(buffer.type==1)
                {
                    
            
                    //receive ACK
                    int start=ack_expected;
                    int count=0;
                    while(1){
                            if(start==buffer.seq_num)
                                break;
                            start=(start+1)%4;
                            count++;
                        }
                    
                    if(count>=3)
                    {
                            verbose("duplicate ACK(DL)");
                            pthread_mutex_lock(&mutex_phy_receive);
                            phy_receive_q.pop();
                            pthread_mutex_unlock(&mutex_phy_receive);
                            // remove from physical layer
                            break;
                    }
                    if(count>0)
                        verbose("read just know one");
                        
                        /*receive the ack, so window ones can be removed!*/
                    for(int h=0;h<=count;h++)
                    {
                            pthread_mutex_lock(&mutex_window_q);
                            window_q.pop();
                            pthread_mutex_unlock(&mutex_window_q);
                            if(queued==0)
                                verbose("queue error (DL)");
                            queued--;
                            ack_expected=(ack_expected+1)%4;
                            // to do in this way, it is easy to keep consistency
                        }
                        /*remove*/
                        pthread_mutex_lock(&mutex_phy_receive);
                        phy_receive_q.pop();
                        pthread_mutex_unlock(&mutex_phy_receive);
                        
                }
                else
                {
                        /*...............*/
                        /*receive data frame*/
                        
                    if(buffer.seq_num==(previous_frame_received+1)%4)
                    {
                            
                        previous_frame_received=(previous_frame_received+1)%4;
                        pthread_mutex_lock(&mutex_phy_receive);
                            /*construct the frame from  */
                            
                            
                        frame_to_message(buffer.data,recv_temp_buff);
                        phy_receive_q.pop();
                        pthread_mutex_unlock(&mutex_phy_receive);
                        send_data(1,buffer.seq_num,"ACK");
                    }
                    else
                    {
                        /*drop the packet*/
                        verbose("drop the packet");
                        pthread_mutex_lock(&mutex_phy_receive);
                        phy_receive_q.pop();
                        pthread_mutex_unlock(&mutex_phy_receive);
                            
                        if(buffer.seq_num==previous_frame_received){
                            verbose("duplicate packet(phy)");
                            send_data(1,buffer.seq_num,"ACK");
                            // inform the receiver duplicate data frame received
                        }
                                                   
                        
                        break;
                        
                        
                }
                
                
            
                }
                
                break;
                /*........................*/
            case(APP):
                /*waiting for place for queueing(otherwise block) 
                 * fetch from dl_send_q, send data(...remeber,window)
                 */
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
                
                pthread_mutex_lock(&mutex_dl_send);
               
                data.clear();
                data=dl_send_q.front();
                dl_send_q.pop();
                
                pthread_mutex_unlock(&mutex_dl_send);
                // get the data
                
                pthread_mutex_lock(&mutex_window_q);
                window_q.push(data);
                pthread_mutex_unlock(&mutex_window_q);
                //push it into window_q 
                
                timers[queued]=current_time();
                /*start timer*/
                queued++;
                
                send_data(0,send_seq_num,data);
                //cout<<"data"<<data<<endl;
                
                /*inc(send_seq_num);*/
                send_seq_num=((send_seq_num+1)%4);
                
                
                
                break;
                
                
            case(TIME_OUT):
                if(queued==0){
                    verbose("0 in queue, however reported timeout");
                    exit(1);
                }
                int start=(send_seq_num+4-queued)%4;
                
                for(int i=0;i<queued;i++){
                    
                    /*fetch from window buffer(packet: data of frame),resend, put it into once again, window_q for storing data part for possible retransmittion */
                    pthread_mutex_lock(&mutex_window_q);
                    data.clear();
                    data=window_q.front();
                    window_q.push(window_q.front());
                    window_q.pop();
                    pthread_mutex_unlock(&mutex_window_q);
                    
                    // send_data(type,seq_num,data)
                    send_data(0,(start+i)%4,data);
                    // in our implementation, sett it to now 
                    verbose("reset timer");
                    timers[i]=current_time();
            
                }
                break;
            }
                
        }
    }

