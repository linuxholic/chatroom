#include <sys/socket.h> //struct sockaddr_in
#include <netinet/in.h> //struct sockaddr
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>

#define SERV_PORT 5000
#define MAX_CLIENTS 100
#define MAX_NAME 24

/* Only the main thread access this global variable */
static int uid = 10;

/* Operations on global variable 'cli_count' */
typedef struct {
	unsigned int count;
	pthread_rwlock_t rwlock;
}cli_count_t;
static cli_count_t cli_count = {0, PTHREAD_RWLOCK_INITIALIZER};

int get_cli_count(void){
	int count;
	pthread_rwlock_rdlock(&cli_count.rwlock);
	count = cli_count.count;
	pthread_rwlock_unlock(&cli_count.rwlock);
	return count;
}

void dec_cli_count(void){
	pthread_rwlock_wrlock(&cli_count.rwlock);
	cli_count.count--;
	pthread_rwlock_unlock(&cli_count.rwlock);
}

void inc_cli_count(void){
	pthread_rwlock_wrlock(&cli_count.rwlock);
	cli_count.count++;
	pthread_rwlock_unlock(&cli_count.rwlock);
}

/* client entity */
typedef struct {
	struct sockaddr_in addr;	/* Client address */
	int connfd;					/* Connection file descriptor */
	int uid;					/* Client unique identifier */
	char name[MAX_NAME];				/* Client name */
} client_t;

/* global clients queue and its lock */
client_t *clients[MAX_CLIENTS];
pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;

/* TODO: Synchronize queue operations */

/* Add client to queue */
void queue_add(client_t *cli){
	int i;
	pthread_rwlock_wrlock(&rwlock);
	for(i=0; i<MAX_CLIENTS; i++){
		if(!clients[i]){
			clients[i] = cli;
			printf("add client %d, bingo!\n", i);
			break;
		}
	}
	pthread_rwlock_unlock(&rwlock);
}

/* Delete client from queue */
void queue_delete(int uid){
	int i;
	pthread_rwlock_wrlock(&rwlock);
	for(i=0; i<MAX_CLIENTS; i++){
		if(clients[i]){
			if(clients[i]->uid == uid){
				clients[i] = NULL;
				break;
			}
		}
	}
	pthread_rwlock_unlock(&rwlock);
}

/* Send message to all clients except the sender*/
void send_message_public(char *mgs, int uid){
	int i;
	pthread_rwlock_rdlock(&rwlock);
	for(i=0; i<MAX_CLIENTS; i++){
		if(clients[i]){
			if(clients[i]->uid != uid){
				write(clients[i]->connfd, mgs, strlen(mgs));
			}
		}
	}
	pthread_rwlock_unlock(&rwlock);
}

/* Send message to all clients including the sender */
void send_message_all(char *mgs){
	int i;
	pthread_rwlock_rdlock(&rwlock);
	for(i=0; i<MAX_CLIENTS; i++){
		if(clients[i]){
			write(clients[i]->connfd, mgs, strlen(mgs));
		}
	}
	pthread_rwlock_unlock(&rwlock);
}

/* Send message to a specific client */
void send_message_private(char *mgs, int uid){
	int i;
	pthread_rwlock_rdlock(&rwlock);
	for(i=0; i<MAX_CLIENTS; i++){
		if(clients[i]){
			if(clients[i]->uid == uid){
				write(clients[i]->connfd, mgs, strlen(mgs));
				break;
			}
		}
	}
	pthread_rwlock_unlock(&rwlock);
}

/* Send message to the sender itself */
void send_message_self(char *mgs, int connfd){
	write(connfd, mgs, strlen(mgs));
}

/* Send list of active clients */
void send_active_clients(int connfd){
	int i;
	char s[64];
	pthread_rwlock_rdlock(&rwlock);
	for(i=0; i<MAX_CLIENTS; i++){
		if(clients[i]){
			sprintf(s, "<<CLIENT %d | %s\r\n", clients[i]->uid, clients[i]->name);
			send_message_self(s, connfd);
		}
	}
	pthread_rwlock_unlock(&rwlock);
}

/* Strip carriage return */
void strip_newline(char *s){
	while(*s != '\0'){
		if(*s == '\n' || *s == '\r'){
			*s = '\0';
		}
		s++;
	}
}

/* Print client ip address */
void print_client_addr(struct sockaddr_in *addr){
	char str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(addr->sin_addr), str, sizeof(str));
	printf("%s", str);
}

/* Handle all communication with the client */
void *handle_client(void *arg){
	char buf_in[1024];	
	char buf_out[1024];
	int rlen;

	/* TODO: Synchronize operations on global variables */
	inc_cli_count();
	client_t *cli = (client_t *)arg;

	printf("<<ACCEPT ");
	print_client_addr(&cli->addr);
	printf(" REFERENCED BY %d\n", cli->uid);

	sprintf(buf_out, "JOIN, HELLO %s\r\n", cli->name);
	send_message_all(buf_out);

	/* Receive input from client */
	while((rlen = read(cli->connfd, buf_in, sizeof(buf_in)-1)) > 0){
		buf_in[rlen] = '\0';
		buf_out[0] = '\0';
		strip_newline(buf_in);

		/* Ignore empty buffer */
		if(!strlen(buf_in)) continue;

		/* Special options */
		if(buf_in[0] == '\\'){
			char *command, *param, *saveptr;
			command = strtok_r(buf_in, " ", &saveptr);
			if(!strcmp(command, "\\QUIT")){
				break;
			}
			else if(!strcmp(command, "\\PING")){
				send_message_self("<<PONG\r\n", cli->connfd);
			}
			else if(!strcmp(command, "\\ACTIVE")){
				sprintf(buf_out, "CLIENTS %d\r\n", get_cli_count());
				send_message_self(buf_out, cli->connfd);
				send_active_clients(cli->connfd);
			}
			else if(!strcmp(command, "\\NAME")){
				param = strtok_r(NULL, " ", &saveptr);
				if(param){
					if(strlen(param) < MAX_NAME){
						char *old_name = strdup(cli->name);
						strcpy(cli->name, param);
						sprintf(buf_out, "<<RENAME, %s TO %s\r\n", old_name, cli->name);
						send_message_all(buf_out);
					}
					else {
						send_message_self("<<NAME IS TOO LONG\r\n", cli->connfd);
					}
				}
				else{
					send_message_self("<<NAME CANNOT BE NULL\r\n", cli->connfd);
				}
			}
			else if(!strcmp(command, "\\PRIVATE")){
				param = strtok_r(NULL, " ", &saveptr);	
				if(param){
					int uid = atoi(param);
					param = strtok_r(NULL, " ", &saveptr);
					if(param){
						sprintf(buf_out, "<<[PM][%s]", cli->name);
						while(param != NULL){
							strcat(buf_out, " ");
							strcat(buf_out, param);
							param = strtok_r(NULL, " ", &saveptr);
						}
						strcat(buf_out, "\r\n");
						send_message_private(buf_out, uid);
					}
					else{
						send_message_self("<<MESSAGE CANNOT BE NULL\r\n", cli->connfd);
					}
				}
				else{
					send_message_self("<<REFERENCE CANNOT BE NULL\r\n", cli->connfd);
				}
			}
			else if(!strcmp(command, "\\HELP")){
				strcat(buf_out, "\\QUIT     Quit chatroom\r\n");
				strcat(buf_out, "\\PING     Server test\r\n");
				strcat(buf_out, "\\NAME     <name> Change nickname\r\n");
				strcat(buf_out, "\\PRIVATE  <reference> <message> Send private message\r\n");
				strcat(buf_out, "\\ACTIVE   Show active clients\r\n");
				strcat(buf_out, "\\HELP     Show help\r\n");
				send_message_self(buf_out, cli->connfd);
			}
			else{
				send_message_self("<<UNKNOWN COMMAND\r\n", cli->connfd);
			}
		}
		else{
			/* Send message */
			sprintf(buf_out, "[%s] %s\r\n", cli->name, buf_in);
			send_message_public(buf_out, cli->uid);
		}
	}

	/* Close connection */
	close(cli->connfd);
	sprintf(buf_out, "<<LEAVE, BYE %s\r\n", cli->name);
	send_message_all(buf_out);

	/* Delete client from queue and terminate thread */
	queue_delete(cli->uid);
	printf("<<LEAVE  ");
	print_client_addr(&cli->addr);
	printf(" REFERENCED BY %d\n", cli->uid);
	free(cli);
	/* TODO: Synchronize operations on global variables */
	dec_cli_count();
	pthread_detach(pthread_self());
	/* After return, its resourse will be freeed immediately.
	   If using pthread_exit(), its low-level resource may be
	   reserved until a call to pthread_join().*/
}

int main(int argc, char *argv[]){
	int listenfd = 0, connfd = 0, n = 0;
	struct sockaddr_in serv_addr;
	struct sockaddr_in cli_addr;
	pthread_t tid;

	/* Socket settings */
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(SERV_PORT);

	/* Bind */
	if(bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
		perror("Socket bingding failed");
		return 1;
	}

	/* Listen */
	if(listen(listenfd, 10) < 0){
		perror("Socket listening failed");
		return -1;
	}

	printf("<[SERVER STARTED]>\n");

	/* Accept clients */
	while(1){
		int clilen = sizeof(cli_addr);
		connfd = accept(listenfd, (struct sockaddr*)&cli_addr, &clilen);

		/* Check if max clients is reached */
		if(get_cli_count() == MAX_CLIENTS){
			printf("<<MAX CLIENTS REACHED\n");
			printf("<<REJECT ");
			print_client_addr(&cli_addr);
			printf("\n");
			close(connfd);
			continue;
		}

		/* Client settings */
		client_t *cli = (client_t *)malloc(sizeof(client_t));
		if(cli == NULL) printf("malloc failed.\n");
		cli->addr = cli_addr;
		cli->connfd = connfd;
		cli->uid = uid++;
		sprintf(cli->name, "%d", cli->uid);

		/* Add client to the queue and fork thread */
		queue_add(cli);
		pthread_create(&tid, NULL, handle_client, (void *)cli);

		/* Reduce CPU usage */
		sleep(1);
	}

	return 0;
}
