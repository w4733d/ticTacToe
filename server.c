/* To compile me in Linux, type:   gcc -o server server.c strmap.c -lpthread */

/*************************************************************************
@author Waleed Khan (waleedkhan.com)

Concurrent server for the Tic Tac Toe Online game. It is based on a program 
from Computer Networks and Internets by Douglas Comer. Server listens 
for incomming connections from players and creates a separate thread for 
each player.
 
Players can query to join, leave, list and invite other players. Program
holds a list of all players that have joined in a hashmap protected by 
a mutex. The hasmap is a modified version of an implementation by 
Per Ola Kristensson (http://pokristensson.com/strmap.html). 
**************************************************************************
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "strmap.h"


void * serverthread(void * parm);       /* Thread function to handle communication with clients */

/* A call back function for the hasmap enumerator that forms a list of players
 * by concatenating them into the obj parameters.
 */ 
void getPlayers(const void *key, const void *value, void *obj);

/* Hashmap to hold player name's as keys 
 * and  and their IPs as values . 
 */
StrMap *players; 

int size =  0; 	/* Sum of users currently joined as players */

pthread_mutex_t  mut; /* Mutex to prevent race conditions. */

#define PROTOPORT         27428        /* default protocol port number */
#define QLEN              10             /* size of request queue        */
#define MAXRCVLEN 200
#define STRLEN 200

int main (int argc, char *argv[])
{
	
	players = sm_new(100);	/* Initialize hashmap */
	
     struct   hostent   *ptrh;     /* pointer to a host table entry */
     struct   protoent  *ptrp;     /* pointer to a protocol table entry */
     struct   sockaddr_in sad;     /* structure to hold server's address */
     struct   sockaddr_in cad;     /* structure to hold client's address */
     int      sd, sd2;             /* socket descriptors */
     int      port;                /* protocol port number */
     int      alen;                /* length of address */
     pthread_t  tid;             /* variable to hold thread ID */

     pthread_mutex_init(&mut, NULL);
     memset((char  *)&sad,0,sizeof(sad)); /* clear sockaddr structure   */
     sad.sin_family = AF_INET;            /* set family to Internet     */
     sad.sin_addr.s_addr = INADDR_ANY;    /* set the local IP address */

     /* Check  command-line argument for protocol port and extract      */
     /* port number if one is specfied.  Otherwise, use the default     */
     /* port value given by constant PROTOPORT                          */
        
     if (argc > 1) {                        /* if argument specified     */
                     port = atoi (argv[1]); /* convert argument to binary*/
     } else {
                      port = PROTOPORT;     /* use default port number   */
     }
     if (port > 0)                          /* test for illegal value    */
                      sad.sin_port = htons((u_short)port);
     else {                                /* print error message and exit */
                      fprintf (stderr, "bad port number %s/n",argv[1]);
                      exit (1);
     }

     /* Map TCP transport protocol name to protocol number */     
     if ( ((int)(ptrp = getprotobyname("tcp"))) == 0)  {
                     fprintf(stderr, "cannot map \"tcp\" to protocol number");
                     exit (1);
     }

     /* Create a socket */
     sd = socket (PF_INET, SOCK_STREAM, ptrp->p_proto);
     if (sd < 0) {
                       fprintf(stderr, "socket creation failed\n");
                       exit(1);
     }

     /* Bind a local address to the socket */
     if (bind(sd, (struct sockaddr *)&sad, sizeof (sad)) < 0) {
                        fprintf(stderr,"bind failed\n");
                        exit(1);
     }
     
     /* Specify a size of request queue */
     if (listen(sd, QLEN) < 0) {
                        fprintf(stderr,"listen failed\n");
                         exit(1);
     }

     alen = sizeof(cad);

     /* Main server loop - accept and handle requests */
     fprintf(stderr, "Server up and running.\n");
     while (1) {         
         if ((sd2=accept(sd, (struct sockaddr *)&cad, &alen)) < 0) {
	                      fprintf(stderr, "accept failed\n");
                              exit(1);
		}
		/* Create seperate thread for each client */
		pthread_create(&tid, NULL, serverthread, (void *) sd2 );
     }
     
     close(sd); /* Close socket */     
     sm_delete(players); /* Delete the hasmap */
}

/* Thread function to handle communication with clients */
void * serverthread(void * parm)
{
	int tsd, len;
	tsd = (int) parm;
	
	/* Get client's IP address*/
	char ip[INET_ADDRSTRLEN];
	struct sockaddr_in peeraddr;
	socklen_t peeraddrlen = sizeof(peeraddr);
	getpeername(tsd, &peeraddr, &peeraddrlen);
	inet_ntop(AF_INET, &(peeraddr.sin_addr), ip, INET_ADDRSTRLEN);
	
	char buf[MAXRCVLEN+1];           /* buffer for data exchange */
	char name[STRLEN+1];  /* Variable to store current client's name. */
	
	/* Run loop until client closes connection. */
	while(len = recv(tsd, buf, MAXRCVLEN, 0))
    {
		/* Split received query into two arguements */
		buf[len] = '\0';		
		char arg1[STRLEN], arg2[STRLEN];
		int n = sscanf(buf, "%s %s", arg1, arg2);
		
		/* Handle 'join' query */
		if(strcmp(arg1, "join") == 0 && arg2 != NULL)
		{
			/* Put the player in hasmap if the player doesn't already exist. */
			pthread_mutex_lock(&mut);
			if(sm_exists(players, arg2) == 0)
			{
				sm_put(players, arg2, ip);
				size++;
				strcpy(name, arg2);
				sprintf(buf, "Player %s added to the player's list\n", arg2);
				printf("Player %s added to the player's list\n", arg2);
			}
			else
			{
				sprintf(buf, "Player %s is already in the player's list\n", arg2);
			}
			pthread_mutex_unlock(&mut);
			send(tsd, buf, strlen(buf), 0);
		}
		/* Handle 'invite {playername}' query. */
		else if (strcmp(arg1, "invite") == 0 && arg2 != NULL)
		{	
			/* If invited player exists then send player's IP address. */
			pthread_mutex_lock(&mut);
			if((size > 0) && (sm_exists(players, arg2) == 1))
			{
				sm_get(players, arg2, buf, sizeof(buf));
			}
			else
			{
				sprintf(buf, "Player %s not found\n", arg2);
			}
			pthread_mutex_unlock(&mut);
			send(tsd, buf, strlen(buf), 0);
		}
		/* Handle 'list' query. */
		else if (strcmp(arg1, "list") == 0)
		{
			/* Iterate over the hasmap and form a list of players to send. */
			pthread_mutex_lock(&mut);
			if(size > 0)
			{
				sprintf(buf, "Players: ");
				sm_enum(players, getPlayers, buf);
			}
			else
			{
				sprintf(buf, "There are no players online\n", arg2);
			}
			pthread_mutex_unlock(&mut);
			send(tsd, buf, strlen(buf), 0);
		}
    }
    
    /* If client closes connection then remove the entry from the hashmap and
     * close the socket and thread.
     */ 
    pthread_mutex_lock(&mut);
	if(name != NULL && strlen(name) > 1)
	{
		sm_get(players, name, buf, sizeof(buf));
		if (strcmp(buf, ip) == 0)
		{			
			/* Important note: Player is not really removed from hashmap,
			 * only the value is removed while the key is kept. This is 
			 * a workaround function as the original hasmap implementation 
			 * did not come with a remove function and I did not have
			 * time to implement it myself.
			 */ 
			sm_remove(players, name);
			size--;
			printf("Player %s removed from the player's list\n", name);
			name[0] = '\0';
		}
	}
	pthread_mutex_unlock(&mut);
	close(tsd);
	pthread_exit(0);
}    

/* A call back function for the hasmap enumerator that forms a list of players
 * by concatenating them into the obj parameter.
 */ 
void getPlayers(const void *key, const void *value, void *obj)
{
	if (value != NULL)
	{
		strcat(obj, key);
		strcat(obj, ", ");
	}
}
