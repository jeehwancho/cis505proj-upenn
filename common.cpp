#include "common.h"
#include "addrlist.h"
#include "mesgqueue.h"

int g_fd, g_fdclient;
char g_leaderinfo[MAXNAME + 20]; //keeps leader/sequencer info
char g_leaderName[MAXNAME];
char g_name[MAXNAME];
char g_server[20];
int g_port;
struct sockaddr_in g_remaddrclient, g_myaddr;
int isLeaderChanged;
int isEOF;
int livecountForSequencer;
int g_seqSend;
int g_seqRecv;

pthread_t g_pid_receive_thread_client;
pthread_t g_keep_alive_thread_client;
pthread_t g_fgets_thread_client;

//sequencer
void DoSequencerMessageQueueOperation(char* recv_data, sockaddr_in recvaddr) {
	int seq;
	char *cmd;
	char seqstr[10], msg[BUFSIZE];
	cmd = strsep(&recv_data, ":");
	strcpy(seqstr, cmd);
	seq = atoi(seqstr);
	strcpy(msg, recv_data);
	int seqRecv = GetSeqRecvByAddr(g_alist, recvaddr);

	if (seq == 0) {
		SequencerController(msg, recvaddr);
		return;
	}
	EnqueueMessageQueue(&g_RecvQueue, msg, seq, recvaddr);

	if (PeekMessageQueue(g_RecvQueue, seqRecv+1, recvaddr) == NULL) {
		char sendbuf[10];
		sprintf(sendbuf, "0:ask:%d", seqRecv+1);
		char send_data_chksum[BUFSIZE];
		sprintf(send_data_chksum, "%d:%s", chash(sendbuf), sendbuf);
		sendto(g_fd, send_data_chksum, strlen(send_data_chksum), 0, 
			(struct sockaddr *)&recvaddr, sizeof(recvaddr));
		return;
	}
	struct mnode *en;
	en = DequeueMessageQueue(&g_RecvQueue, seqRecv+1, recvaddr);
	while (en != NULL) {
		SetSeqRecvByAddr(g_alist, recvaddr, ++seqRecv);
		SequencerController(en->mesg, recvaddr);
		en = DequeueMessageQueue(&g_RecvQueue, seqRecv+1, recvaddr);
	}
	return;
}

//controller for sequencer
void SequencerController(char* recv_data, sockaddr_in addr){
	char *cmd;
	//if msg has seq# 0 which is meaningless, ignore seq#.
	//e.g., 0:rec:name can be sent to either sequencer and client
	cmd = strsep(&recv_data, ":");
	//when a client joins
	//register ip, port, name etc
	//in-protocol: rec:name
	if (strcmp(cmd, "rec") == 0) {
		char listbuffer[BUFSIZE];
		char buffer[BUFSIZE];
		//first notice all other client EXCEPT the one that just joined
		//NOTICE Alice joined on 192.168.5.81:1923
		//out-protocol: msg:send_data
		sprintf(buffer, "msg:NOTICE %s joined on %s:%d\n", recv_data, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
		MultiCast(buffer);
		memset((char *)&buffer, 0, sizeof(buffer));

		//add addr to address list		
		Push(&g_alist, addr, recv_data);
		
		//send upd:name1:ip1:port1:name2:ip2:port2:...:end
		GetUpdateList(buffer);
		MultiCast(buffer);

		//out-protocol: res:clientip:clientport:userlist
		ShowListWithLeader(listbuffer);
		int seq = GetSeqSendByAddr(g_alist, addr);
		SetSeqSendByAddr(g_alist, addr, ++seq);
		sprintf(buffer, "%d:res:%s:%d:%s", seq, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), listbuffer);

		char send_data_chksum[BUFSIZE];
		sprintf(send_data_chksum, "%d:%s", chash(buffer), buffer);

		EnqueueMessageQueue(&g_SendQueue, send_data_chksum, seq, addr);
		//send a list of users to the client
		sendto(g_fd, send_data_chksum, strlen(send_data_chksum), 0, 
			(struct sockaddr *)&addr, sizeof(addr));
	}
	//msg
	//in-protocol: msg:message
	else if (strcmp(cmd, "msg") == 0) {
		//get the name
		char name[MAXNAME];
		GetNameByAddr(g_alist, addr, name);
		//form msg to send
		//out-protocol: msg:name:: MessageToMulticast
		char send_data[BUFSIZE];
		sprintf(send_data, "msg:%s:: %s", name, recv_data);
		//multicast that msg to everyone that is connected
		MultiCast(send_data);
	}
	else if (strcmp(cmd, "kpa") == 0){
		char name[MAXNAME];
		GetNameByAddr(g_alist, addr, name);
		ZeroizeLiveCount(g_alist, name);
	}
	else if (strcmp(cmd, "ask") == 0){
		struct mnode* en;
		en = DequeueMessageQueue(&g_SendQueue, atoi(recv_data), addr);
		if (en != NULL) {
			EnqueueMessageQueue(&g_SendQueue, en->mesg, en->seqNum, en->addr);
			sendto(g_fd, en->mesg, strlen(en->mesg), 0, 
				(struct sockaddr *)&addr, sizeof(addr));
		}
	}
	else {
		printf("nothing\n");
	}
	return;
}

//multicast to those in client list
void MultiCast(char* msg){
	struct anode* current = g_alist;
	char msgwithseq[BUFSIZE];
	int DoWeGetSeqNumber = 0;
	int seq;
	if (msg[0] == 'z' && msg[1] == 'e' && msg[2] == 'r') {
		DoWeGetSeqNumber = 1;
	}
	//multicasting for all the registered clients
	while (current != NULL) {
		//do multicast
		if (DoWeGetSeqNumber){
			seq = 0;
		} else {
			seq = GetSeqSendByAddr(g_alist, current->addr);
			SetSeqSendByAddr(g_alist, current->addr, ++seq);
		}
		sprintf(msgwithseq, "%d:%s", seq, msg);

		char send_data_chksum[BUFSIZE];
		sprintf(send_data_chksum, "%d:%s", chash(msgwithseq), msgwithseq);
		if (seq != 0) {
			EnqueueMessageQueue(&g_SendQueue, send_data_chksum, seq, current->addr);
		}
		sendto(g_fd, send_data_chksum, 
			strlen(send_data_chksum), 
			0, 
			(struct sockaddr *)&(current->addr), 
			sizeof(current->addr));
		current = current->next;
	}
	//at last, sequencer gets the msg as well
	char *cmd;
	cmd = strsep(&msg, ":");
	if (strcmp(cmd, "msg") == 0)
		printf("%s", msg);
	return;
}

//get client list
void ShowListWithLeader(char* buffer){	
	char alistbuffer[BUFSIZE];
	ShowList(g_alist, alistbuffer);
	sprintf(buffer, "%s (Leader)\n%s", g_leaderinfo, alistbuffer);
	return;	
}

//get upd message for sequencer
void GetUpdateList(char* buffer){
	char line[MAXNAME + 20];
	char *leadername, *leaderinfo;
	leaderinfo = g_leaderinfo;
	leadername = strsep(&leaderinfo, " ");
	sprintf(buffer, "upd:%s:", leadername);
	struct anode* current = g_alist;
	while (current != NULL) {
		sprintf(line, "%s:%s:%d:", 
			current->name, 
			inet_ntoa(current->addr.sin_addr), 
			ntohs(current->addr.sin_port));
		strcat(buffer, line);
		current = current->next;
	}
	strcat(buffer, "end");
	return;
}

//client
void DoClientMessageQueueOperation(char* recv_data, sockaddr_in recvaddr) {
	int seq;
	char *cmd;
	char seqstr[10], msg[BUFSIZE];
	cmd = strsep(&recv_data, ":");
	strcpy(seqstr, cmd);
	seq = atoi(seqstr);
	strcpy(msg, recv_data);
	
	if (msg[0] == 'z' && msg[1] == 'e' && msg[2] == 'r') {
		g_seqSend = 0;
		g_seqRecv = 0; //new leader elected
	}
	if (seq == 0) { //ignore seq#
		ClientController(msg, recvaddr);
	}
	if (g_seqRecv >= seq) { //duplicated
		return;
	}
	EnqueueMessageQueue(&g_RecvQueue, msg, seq, recvaddr);
	struct mnode* en;
	en = DequeueMessageQueue(&g_RecvQueue, g_seqRecv+1, recvaddr);
	while (en != NULL) {
		g_seqRecv++;
		ClientController(en->mesg, en->addr);
		en = DequeueMessageQueue(&g_RecvQueue, g_seqRecv+1, recvaddr);
	}
	if (MsgQCount(g_RecvQueue) != 0) {
		char sendbuf[10];
		sprintf(sendbuf, "0:ask:%d", g_seqRecv+1);
		char send_data_chksum[BUFSIZE];
		sprintf(send_data_chksum, "%d:%s", chash(sendbuf), sendbuf);
		sendto(g_fdclient, send_data_chksum, strlen(send_data_chksum), 0, 
			(struct sockaddr *)&recvaddr, sizeof(recvaddr));
	}
	return;
}
//controller for client
void ClientController(char* recv_data, sockaddr_in recvaddr){
	char *cmd;
	cmd = strsep(&recv_data, ":");

	//from server/sequencer
	//response to register req, it should get user name
	//in-protocol: res:clientip:clientport:userlist
	if (strcmp(cmd, "res") == 0){
		char myip[20];
		char myport[10];
		char namewithleader[MAXNAME+10];
		cmd = strsep(&recv_data, ":");
		strcpy(myip, cmd);
		cmd = strsep(&recv_data, ":");
		strcpy(myport, cmd);
		cmd = strsep(&recv_data, "\n");
		strcpy(namewithleader, cmd);
		printf("%s joining a new chat on %s:%d, listening on %s:%s\n", g_name, g_server, g_port, myip, myport);
		printf("Succeeded, current users:\n");
		printf("%s %s:%d\n", namewithleader, g_server, g_port);
		printf("%s", recv_data);
	}
	//from client to another client requesting re-direction to sequencer
	else if (strcmp(cmd, "rec") == 0) {
		char send_data[BUFSIZE];
		sprintf(send_data, "0:red:%s:%s:%d", g_leaderName, g_server, g_port);

		char send_data_chksum[BUFSIZE];
		sprintf(send_data_chksum, "%d:%s", chash(send_data), send_data);

		socklen_t slen = sizeof(recvaddr);
		if (sendto(g_fdclient, send_data_chksum, strlen(send_data_chksum), 0, (struct sockaddr *)&recvaddr, slen)==-1) {
			perror("sendto");
			exit(1);
		}
	}
	//redirecting
	else if (strcmp(cmd, "red") == 0) {
		char strport[10];
		cmd = strsep(&recv_data, ":");
		strcpy(g_leaderName, cmd);
		cmd = strsep(&recv_data, ":");
		strcpy(g_server, cmd);
		cmd = strsep(&recv_data, ":");
		strcpy(strport, cmd);
		g_port = atoi(strport);
		//update remote addr
		memset((char *) &g_remaddrclient, 0, sizeof(g_remaddrclient));
		g_remaddrclient.sin_family = AF_INET;
		g_remaddrclient.sin_port = htons(g_port);
		if (inet_aton(g_server, &g_remaddrclient.sin_addr)==0) {
			fprintf(stderr, "inet_aton() failed\n");
			exit(1);
		}
		//send rec to real server/sequencer this time
		char send_data[BUFSIZE];
		sprintf(send_data, "0:rec:%s", g_name);

		char send_data_chksum[BUFSIZE];
		sprintf(send_data_chksum, "%d:%s", chash(send_data), send_data);

		socklen_t slen = sizeof(g_remaddrclient);
		if (sendto(g_fdclient, send_data_chksum, strlen(send_data_chksum), 0, (struct sockaddr *)&g_remaddrclient, slen)==-1) {
			perror("sendto");
			exit(1);
		}
	}
	//in-protocol: msg:MessageToThisClient
	else if (strcmp(cmd, "msg") == 0) {
		printf("%s", recv_data);		
	}
	else if (strcmp(cmd, "upd") == 0) {
		UpdateClientList(recv_data);
	}
	else if (strcmp(cmd, "upl") == 0) {
		strcpy(g_name, recv_data);
		isLeaderChanged = 1;
		strcpy(g_server, inet_ntoa(recvaddr.sin_addr));
		g_port = ntohs(recvaddr.sin_port);
	}
	else if (strcmp(cmd, "ask") == 0){
		struct mnode* en;
		en = DequeueMessageQueue(&g_SendQueue, atoi(recv_data), recvaddr);
		if (en != NULL) {
			EnqueueMessageQueue(&g_SendQueue, en->mesg, en->seqNum, en->addr);
			sendto(g_fd, en->mesg, strlen(en->mesg), 0, 
				(struct sockaddr *)&recvaddr, sizeof(recvaddr));
		}
	}
	else if (strcmp(cmd, "kpa") == 0) {
		if (strcmp(recv_data, "KEEP_ALIVE") == 0){
			char alive[20];
			sprintf(alive, "%d:kpa:ALIVE", ++g_seqSend);

			char send_data_chksum[BUFSIZE];
			sprintf(send_data_chksum, "%d:%s", chash(alive), alive);
			EnqueueMessageQueue(&g_SendQueue, send_data_chksum, g_seqSend, recvaddr);
			socklen_t slen = sizeof(recvaddr);
			if (sendto(g_fdclient, send_data_chksum, strlen(send_data_chksum), 0, (struct sockaddr *)&recvaddr, slen)==-1) {
				perror("sendto");
				exit(1);
			}
			livecountForSequencer = 0;
		}
	}
	return;
}

// update client list using upd message
// name:ip:port:...:end
void UpdateClientList(char* recv_data){
	char *token;
	char name[MAXNAME], ip[20], port[10];
	//initialize the client list
	DeleteList(&g_alist);

	token = strsep(&recv_data, ":");
	strcpy(g_leaderName, token);

	//add clients to the list
	while ((token = strsep(&recv_data, ":")) != NULL){
		if (strcmp(token, "end") == 0)
			break;
		strcpy(name, token);
		if ((token = strsep(&recv_data, ":")) == NULL){
			printf("error1\n");
		}
		strcpy(ip, token);
		if ((token = strsep(&recv_data, ":")) == NULL){
			printf("error2\n");
		}
		strcpy(port, token);
		Push(&g_alist, ip, atoi(port), name);
	}
}

//leader is whoever last joined in the chat
//return port number of the new leader
int LeaderElection(char* name, char* leaderName) {
	struct anode* current = g_alist;
	if (NULL == g_alist) {
		return -1;
	}
	char minName[MAXNAME];
	int minPort;
	strcpy(minName, "");
	while (current != NULL) {
		if ((strcmp(minName, "") == 0) ||
			(strcmp(minName, current->name) > 0)) {
			strcpy(minName, current->name);
			minPort = current->addr.sin_port;
		}
		current = current->next;
	}
	strcpy(leaderName, minName);
	if (strcmp(name, minName) == 0) {
		return ntohs(minPort);
	}
	return 0;
}

int CheckSum(char* recv_data, char* out_data) {
	char *cmd;
	cmd = strsep(&recv_data, ":");
	int checksum;
	if (!isdigit(cmd[0]) && cmd[0] != '-') {
		return 0;
	}
	checksum = atoi(cmd);
	strcpy(out_data, recv_data);
	return chash(out_data) == checksum;
}

//Stroustrup's book
//ref: http://stackoverflow.com/questions/2535284/how-can-i-hash-a-string-to-an-int-using-c
int chash(const char *str)
{
    int h = 0;
    while (*str)
       h = h << 1 ^ *str++;
    return h;
}