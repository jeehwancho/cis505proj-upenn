#include "mesgqueue.h"
#include <sys/socket.h>
#include <ifaddrs.h> 
#include "string.h"
 
struct mnode* g_SendQueue = NULL;
struct mnode* g_RecvQueue = NULL;

void EnqueueMessageQueue(struct mnode** headRef, char *mesg, int seqNum, sockaddr_in addr) {
  //adding
  struct mnode* newNode = new mnode;
  memcpy(&(newNode->addr), &addr, sizeof(newNode->addr));
  strcpy(newNode->mesg, mesg);
  newNode->seqNum = seqNum;
  newNode->next = *headRef;
  *headRef = newNode;

  //removing if it exceeds max#
  if (MsgQCount(*headRef) > MESGQUEUESIZE){
    struct mnode* current = *headRef;
    while((current->next)->next != NULL){
      current = current->next;
    }
    struct mnode* toRemove = current->next;
    current->next = NULL;
    delete(toRemove);
  }
  return;
}
 
struct mnode* PeekMessageQueue(struct mnode* headRef, int seqNum, struct sockaddr_in addr){

  struct mnode* current = headRef;
  //struct mnode* prev;

  // handle NULL case                                                                                                                                                               
  if (current==NULL){
    return NULL;
  }

  while(current!=NULL){
    if (current->seqNum==seqNum) {
      if (memcmp(&(current->addr), &addr, sizeof(addr)) == 0){
        return current;
      }
    }
    current = current->next;
  }

  return NULL;

}

struct mnode* DequeueMessageQueue(struct mnode** headRef, int seqNum, struct sockaddr_in addr){

  struct mnode* current = *headRef;
  struct mnode* prev;

  // handle NULL case
  if (current==NULL){
    return NULL;
  }

  // if head is the node to be removed
  if (current->seqNum==seqNum){
    if (memcmp(&(current->addr), &addr, sizeof(addr)) == 0){
      struct mnode* temp = *headRef;
      *headRef = temp->next;
      return current;

    }
  }

  // else we need to find the node and remove it
  while(current!=NULL){
    prev = current;
    current = current->next;
    if (current==NULL) return NULL;
    if (current->seqNum==seqNum) {
      if (memcmp(&(current->addr), &addr, sizeof(addr)) == 0){
        prev->next = current->next;
        return current;
      }
    }
  }
  return NULL;

}
 
void Show(struct mnode* head, char* buffer){
  char line[MSGSIZE];
  strcpy(buffer, "");
  struct mnode* current = head;
  char shortmsg[31];
  while (current != NULL) {
    memcpy(&shortmsg, &(current->mesg), 30);
    sprintf(line, "***%d:%s:%d\n", 
      current->seqNum,
      shortmsg, 
      ntohs(current->addr.sin_port));
    strcat(buffer, line);
    current = current->next;
  }
}
 
void RemoveMessage(struct mnode** headRef, int seqNum, struct sockaddr_in addr){
  DequeueMessageQueue(headRef, seqNum, addr);
}
 
void RemoveEntireMessage(struct mnode** headRef){
 
  struct mnode* current = *headRef;
  struct mnode* next;
  while(current != NULL){
    next = current->next;
    delete(current);
    current = next;
  }
  *headRef = NULL;
  return;
 
}
 
int MsgQCount(struct mnode* headRef){
 
  int result = 0;
  struct mnode* current = headRef;
  if (headRef==NULL) {
    return result;
  }
 
  while(current!=NULL){
    current = current->next;
    result++;
  }
  return result;
 
}
 
int IsEmpty(struct mnode* head){
 
  // if EMPTY, returns 1, else 0;
  if (head==NULL) return 1;
  else return 0;
 
}
 
void mesgtest(){
 
  // struct ifaddrs *ifap, *ifa;
  // struct sockaddr_in *sa;
  // char *addr;
  // getifaddrs (&ifap);
  // for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
  //   if (ifa->ifa_addr->sa_family==AF_INET) {
  //     sa = (struct sockaddr_in *) ifa->ifa_addr;
  //     addr = inet_ntoa(sa->sin_addr);
  //     if (strcmp(addr, "127.0.0.1") != 0 && !(addr[0]=='1' && addr[1]=='9' && addr[2]=='2')){
  //   //printf("Interface: %s\tAddress: %s\n", ifa->ifa_name, addr);
  //   break;
  //     }
  //   }
  // }
  // freeifaddrs(ifap);
 
  // // start testing here!!
  // struct mnode* alist = NULL;
   
  // char mesg1[MAXNAME];
  // char mesg2[MAXNAME];
  // char mesg3[MAXNAME];
  // char buffer1[BUFSIZE];
  // char buffer2[BUFSIZE];
  // strcpy(mesg1, "blah");
  // strcpy(mesg2, "foo");
  // strcpy(mesg3, "bar");
  // EnqueueMessageQueue(&alist, mesg1, 1, *sa);
  // EnqueueMessageQueue(&alist, mesg2, 2, *sa);
  // EnqueueMessageQueue(&alist, mesg3, 3, *sa);
 
  // sa->sin_port = 4;
 
  // struct mnode* temp = PeekMessageQueue(alist, 2, *sa);
 
  // if (temp==NULL) printf("temp is NULL!\n");
  // else printf("%s\n", temp->mesg);
 
 
  // RemoveMessage(&alist, 1);
  // DequeueMessageQueue(&alist, 2, *sa);
  // RemoveMessage(&alist, 3);
 
  //  Show(alist, buffer1);
  //  //   printf("%s", buffer1);
 
  //  RemoveEntireMessage(&alist);
    
  //  EnqueueMessageQueue(&alist, mesg2, 2, *sa);
 
  //  sa->sin_port = 4;
 
 
  //  RemoveEntireMessage(&alist);
  //  RemoveEntireMessage(&alist);
 
  //  EnqueueMessageQueue(&alist, mesg1, 1, *sa);
  //  EnqueueMessageQueue(&alist, mesg2, 2, *sa);
  //  EnqueueMessageQueue(&alist, mesg3, 3, *sa);
 
  //  Show(alist, buffer2);
  //  //  printf("%s", buffer2);
  //  // printf("%d\n", Count(alist));
  //   /*
  // printf("count: %d\n", CountList(alist));
  // DeleteList(&alist);
  // ShowList(alist, buffer);
  // printf("%s", buffer);
  // */
 
  return;
 
 
}