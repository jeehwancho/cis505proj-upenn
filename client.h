#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
int DoClientWork(char* name, char* ipport);
void ClientController(char*, sockaddr_in);
void UpdateClientList(char*);

int LeaderElection(char* name, char* leaderName);