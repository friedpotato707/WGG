#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>

#include "socket.h"
#include "gameplay.h"
#include <signal.h>


#ifndef PORT
    #define PORT 58474
#endif
#define MAX_QUEUE 5


void add_player(struct client **top, int fd, struct in_addr addr);
void remove_player(struct client **top, int fd);

/* These are some of the function prototypes that we used in our solution 
 * You are not required to write functions that match these prototypes, but
 * you may find the helpful when thinking about operations in your program.
 */

void help_disconnect(struct client *p, struct game_state *game);

/* The set of socket descriptors for select to monitor.
 * This is a global variable because we need to remove socket descriptors
 * from allset when a write to a socket fails.
 */
fd_set allset;


/* Add a client to the head of the linked list
 */
void add_player(struct client **top, int fd, struct in_addr addr) {
    struct client *p = malloc(sizeof(struct client));

    if (!p) {
        perror("malloc");
        exit(1);
    }

    printf("Adding client %s\n", inet_ntoa(addr));

    p->fd = fd;
    p->ipaddr = addr;
    p->name[0] = '\0';
    p->in_ptr = p->inbuf;
    p->inbuf[0] = '\0';
    p->next = *top;
    *top = p;
}

/* Removes client from the linked list and closes its socket.
 * Also removes socket descriptor from allset 
 */
void remove_player(struct client **top, int fd) {
    struct client **p;

    for (p = top; *p && (*p)->fd != fd; p = &(*p)->next)
        ;
    // Now, p points to (1) top, or (2) a pointer to another client
    // This avoids a special case for removing the head of the list
    if (*p) {
        struct client *t = (*p)->next;
        printf("Removing client %d %s\n", fd, inet_ntoa((*p)->ipaddr));
        FD_CLR((*p)->fd, &allset);
        close((*p)->fd);
        free(*p);
        *p = t;
    } else {
        fprintf(stderr, "Trying to remove fd %d, but I don't know about it\n",
                 fd);
    }
}


/* Move the has_next_turn pointer to the next active client */
void advance_turn(struct game_state *game){
    if(game->has_next_turn->next != NULL){
        game->has_next_turn = game->has_next_turn->next;
    }
    else{
        game->has_next_turn = game->head;
    }
}


/* Ask client whose turn it is for guess
 */
void prompt_for_guess(struct game_state *game){
    // prompt client whose turn it is to type guess
    if(game->has_next_turn != NULL){
        char msg[MAX_BUF] = "Your guess?\n";
        int len1 = strlen(msg);
        msg[len1] = '\r'; 
        if (write(game->has_next_turn->fd, msg, len1 + 1) != len1 + 1) {
            // there is a problem with socket
             help_disconnect(game->has_next_turn, game);
       }
    }
}


/* Broadcast message to all the active clients. 
 * Outbuf must have a null terminating character
 */
void broadcast(struct game_state *game, char *outbuf){
    int len = strlen(outbuf);
    outbuf[len] = '\r';
    outbuf[len + 1] = '\n';

    for(struct client *p = game->head; p != NULL;) {        
        struct client *next = p->next;
        if (write(p->fd, outbuf, len + 2) != len + 2) {
            // socket is invalid
            help_disconnect(p, game);
        }
        p = next;
    }       
}


/* Announce whose turn it is to client p. */
void announce_turn(struct game_state *game, struct client *p){
    // get the status message for the current state of the game
    if(game->has_next_turn != NULL && p != NULL){
        // there is a current player, so announce turn
        char game_info[MAX_MSG];
        status_message(game_info, game);
        int info_len = strlen(game_info);
        game_info[info_len] = '\r';
        game_info[info_len + 1] = '\n';

        if (write(p->fd, game_info, info_len + 2) != info_len + 2) {
             // socket is invalid
            help_disconnect(p, game);
        }

        // annouce turn to all the players
        char all_msg[MAX_BUF];
        char p1[] = "It's ";
        int len0 = strlen(p1);
        strcpy(all_msg, p1);

        int len1 = strlen(game->has_next_turn->name);
        strcat(all_msg, game->has_next_turn->name);

        char p3[] = "'s turn.";
        int len2 = strlen(p3);
        strcat(all_msg, p3);
        int total = len0 + len1 + len2;
        all_msg[total] = '\r';
        all_msg[total + 1] = '\n';
        
        if(p != game->has_next_turn){                 
            if (write(p->fd, all_msg, total + 2) != total + 2) {
                // socket is invalid
                help_disconnect(p, game);
            }
        }
    }
}


/* Announce the winner of the game */
int announce_winner(struct game_state *game){
    if(game->head != NULL){
        if(game->guesses_left == 0){
            // inform players that game is over
            char all_msg[MAX_BUF];
        
            char no_more_text[] = "Game over! No more guesses left. The word was ";
            int len0 = strlen(no_more_text);
            strcpy(all_msg, no_more_text);

            int len1 = strlen(game->word);
            strcat(all_msg, game->word);

            all_msg[len0 + len1] = '\0';

            printf("%s\n", all_msg);

            broadcast(game, all_msg);

            return 1;                                        
        }
        else if(strcmp(game->word, game->guess) == 0){
            printf("Game over! %s won.\n", game->has_next_turn->name);

            // client won
            // inform players that game is over
            char all_msg[MAX_BUF];
 
            char game_over[] = "Game over! ";
            int len = strlen(game_over);
            strcpy(all_msg, game_over);

            int len0 = strlen(game->has_next_turn->name);
            strcat(all_msg, game->has_next_turn->name);

            char won[] = " won.\r\n \r\n";
            int len1 = strlen(won);
            strcat(all_msg, won);

            int total = len + len0 + len1;
          
            for(struct client *p = game->head; p != NULL;) {
                struct client *next = p->next;
                // send message to all the clients except the winner
                if(p != game->has_next_turn){
                    if (write(p->fd, all_msg, total) != total) {
                        // socket is invalid
                        help_disconnect(p, game);
                    }
                } 
                p = next;   
            }       

            // inform the winner
            char win_message[MAX_BUF];
            strcpy(win_message, game_over);
            strcat(win_message, "You won.\r\n \r\n");
            total = len + strlen("You won.\r\n \r\n");
        
            if (write(game->has_next_turn->fd, win_message, total) != total) {
                // socket is invalid
                help_disconnect(game->has_next_turn, game);
            }            
            return 1;       
        }
    }
    return 0;
}


/* Return position of a network newline in a buffer.
 */
int find_network_newline(const char *buf, int n) {
    for(int i = 0; i < n; i++){
        if(buf[i] == '\r'){
            return i;
        }
    }
    return -1;
}


/* Return whether input was read from a given client p. is_active must be 
 * either 1 or 0 and it indicates whehter p is an active client.
 */
int read_from(struct client *p, struct game_state *game, struct client **new_players, int is_active){
    if(p != NULL){
        // read input from active client
        int index = p->in_ptr - p->inbuf;
        int size_left = MAX_BUF - 1 - index;

        // if buffer is full
        if(size_left == 0){
            size_left = MAX_BUF - 1;
            p->in_ptr = p->inbuf;
        }

        int num_read = read(p->fd, p->in_ptr, size_left);

        printf("[%d] Read %d bytes\n", p->fd, num_read);
    
        if(num_read <= 0){
            // problem with socket
            if(is_active){
                help_disconnect(p, game);
            }
            else{
                remove_player(new_players, p->fd);
            }   
        }
        else{
            if(find_network_newline(p->inbuf, p->in_ptr + num_read - p->inbuf) != -1){
                *(p->in_ptr + num_read - 2) = '\0';            

                p->in_ptr = p->inbuf;

                printf("[%d] Found newline %s\n", p->fd, p->inbuf);

                return 1;                                                         
            }
            else{
                p->in_ptr += num_read;
                return 0;
            }
        }
    }
    return 0;    
}


/* Notify active clients about client p's guess.
 */
void announce_guess(struct client *p, char guess, struct game_state *game){
    if(p != NULL){
        char all_mes[MAX_BUF];

        int len0 = strlen(p->name);
        strcpy(all_mes, p->name);

        char guesses_text[] = " guesses  ";
        int len1 = strlen(guesses_text);
        strcat(all_mes, guesses_text);

        all_mes[len0 + len1 - 1] = guess;
        all_mes[len0 + len1] = '\0';

        broadcast(game, all_mes);
    }    
}


/* Manage turns, remove player p from active clients and inform
 * all the active clients about removal.
 */
void help_disconnect(struct client *p, struct game_state *game){

    // store name of p for future use
    char name[MAX_NAME];
    strcpy(name, p->name);
    int current_left = 0;
    // if p has next turn
    if(game->has_next_turn == p){
        // give turn to next player
        advance_turn(game);
        current_left = 1;
        // if no players left in the game
        if(game->has_next_turn == p){
            game->has_next_turn = NULL;
        }
    }

    // remove p from active clients
    remove_player(&(game->head), p->fd);

    //say goodbye to player who just left
    char all_mes[MAX_BUF];

    char bye_text[] = "Goodbye ";
    int len0 = strlen(bye_text);
    strcpy(all_mes, bye_text);

    int len1 = strlen(name);
    strcat(all_mes, name);

    all_mes[len0 + len1] = '\0';
    broadcast(game, all_mes);

    if(current_left){
        // if player whose turn it was left
        if(game -> has_next_turn != NULL){
            printf("It's %s's turn.\n", game->has_next_turn->name);
        }
        for(struct client *q = game->head; q != NULL;) {
            struct client *next = q->next;
            announce_turn(game, q); 
            q = next;    
        } 
    }    
    
    prompt_for_guess(game);
}


/* Remove p from new players and add to active clients.
 */
void add_to_game(struct client *p, struct client **new_players, struct game_state *game){
    // remove client from new_players
    if(p == *new_players){
    // if p is the first client in the list of new players
        (*new_players) = p->next;
    }
    else{
        struct client *c;
        for(c = *new_players; c != NULL && c->next != p; c = c->next);  
        if(c != NULL){
            c->next = p->next;
        }    
    }  

    // update the name of the client
    strncpy(p->name, p->inbuf, MAX_NAME);

    // add client to game
    p->next = game->head;
    game->head = p;
    p->in_ptr = p->inbuf;

    // if there is no current player
    if(game->has_next_turn == NULL){
        game->has_next_turn = p;  
        printf("It's %s's turn.\n", game->has_next_turn->name);                                   
    }                             

    // inform all the players that p has joined the game
    char all_msg[MAX_BUF];

    int len0 = strlen(p->inbuf);
    strcpy(all_msg, p->name);

    printf("%s jas just joined.\n", p->name);

    char joined_text[] = " has joined the game.";
    int len1 = strlen(joined_text);
    strcat(all_msg, joined_text);
    // broadcast message to active clients
    all_msg[len0 + len1] = '\0';
    broadcast(game, all_msg);
   
   // send information about game to p
   announce_turn(game, p);

    // prompt player whose turn it is for guess
    prompt_for_guess(game);    
}


/* Return -1 if it's not p's turn to guess, otherwise return 1 if p's guess is valid 
 * and 0 if p's guess is invalid. Inform player p if guess is invalid or if it's not
 * p's turn.
 */
int is_valid_input(struct client *p, struct game_state *game){
    int is_valid = 1;
    if(game->has_next_turn != p){
        is_valid = -1;
    }
    else if(strlen(p->inbuf) > 1){
        is_valid = 0;
    }
    else if('a' > p->inbuf[0] || 'z' < p->inbuf[0]){
        is_valid = 0;
    }
    else{
        for(int i = 0; i < NUM_LETTERS; i++){
            if(game->letters_guessed[i] == p->inbuf[0]){
                is_valid = 0;
                break;
            }
        }
    }

    if(is_valid == -1){
        printf("Player %s tried to guess out of turn\n", p->name);

        // inform client that it's not their turn
        char msg[MAX_BUF] = "It's not your turn.\r\n";
        int len = strlen(msg);
        if (write(p->fd, msg, len) != len) {
            // there is a problem with socket
            help_disconnect(p, game);
        }                                                                   
    }
    else if(is_valid == 0){
        // inform client that guess isn't valid
        char msg[MAX_BUF] = "Invalid guess.\r\n";
        int len = strlen(msg);
        if (write(p->fd, msg, len) != len) {
            // there is a problem with socket
            help_disconnect(p, game);                                    
        }
        else{
            prompt_for_guess(game);  
        }                                       
    }

    return is_valid;
}


/* Restart the game and inform active clients about it.
 */
void restart_game(struct game_state *game, char *words){

    char restart_mes[MAX_BUF];

    char new_game[] = "\r\nLet's start a new game.";
    strcpy(restart_mes, new_game);

    broadcast(game, restart_mes);

    printf("Started new game\n");

    init_game(game, words);    
}


/* Process guess, advance turn if guess is incorrect, make announcements to active players. 
 */
void process_guess(struct client *p, struct game_state *game, char *words_filename, int is_correct, char guess){
    announce_guess(p, guess, game);

    // announce winner, if there is one
    int game_over = announce_winner(game);

    if(game_over){
        restart_game(game, words_filename);
    }

    if(!is_correct){
        // if guess is incorrect, advance turn
        advance_turn(game);
    }

    if(game -> has_next_turn != NULL){
        printf("It's %s's turn.\n", game->has_next_turn->name);
    }    

    for(struct client *q = game->head; q != NULL;) {
        struct client *next = q->next;
        announce_turn(game, q); 
        q = next;    
    }    

    prompt_for_guess(game);     
}


int main(int argc, char **argv) {
    int clientfd, maxfd, nready;
    struct client *p;
    struct sockaddr_in q;
    fd_set rset;
    
    if(argc != 2){
        fprintf(stderr,"Usage: %s <dictionary filename>\n", argv[0]);
        exit(1);
    }

    // Add the following code to main in wordsrv.c:
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if(sigaction(SIGPIPE, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }    
    
    // Create and initialize the game state
    struct game_state game;

    srandom((unsigned int)time(NULL));
    // Set up the file pointer outside of init_game because we want to 
    // just rewind the file when we need to pick a new word
    game.dict.fp = NULL;
    game.dict.size = get_file_length(argv[1]);

    init_game(&game, argv[1]);
    
    // head and has_next_turn also don't change when a subsequent game is
    // started so we initialize them here.
    game.head = NULL;
    game.has_next_turn = NULL;
      
    /* A list of client who have not yet entered their name.  This list is
     * kept separate from the list of active players in the game, because
     * until the new playrs have entered a name, they should not have a turn
     * or receive broadcast messages.  In other words, they can't play until
     * they have a name.
     */
    struct client *new_players = NULL;
    
    struct sockaddr_in *server = init_server_addr(PORT);
    int listenfd = set_up_server_socket(server, MAX_QUEUE);
    
    // initialize allset and add listenfd to the
    // set of file descriptors passed into select
    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    // maxfd identifies how far into the set to search
    maxfd = listenfd;

    while (1) {
        // make a copy of the set before we pass it into select
        rset = allset;
        nready = select(maxfd + 1, &rset, NULL, NULL, NULL);
        if (nready == -1) {
            perror("select");
            continue;
        }

        if (FD_ISSET(listenfd, &rset)){
            printf("A new client is connecting\n");
            clientfd = accept_connection(listenfd, &q);

            FD_SET(clientfd, &allset);
            if (clientfd > maxfd) {
                maxfd = clientfd;
            }

            printf("Connection from %s\n", inet_ntoa(q.sin_addr));
            add_player(&new_players, clientfd, q.sin_addr);
            char *greeting = WELCOME_MSG;
            if(write(clientfd, greeting, strlen(greeting)) == -1) {
                fprintf(stderr, "Write to client %s failed\n", inet_ntoa(q.sin_addr));
                remove_player(&new_players, clientfd);
            };
        }      
        
        /* Check which other socket descriptors have something ready to read.
         * The reason we iterate over the rset descriptors at the top level and
         * search through the two lists of clients each time is that it is
         * possible that a client will be removed in the middle of one of the
         * operations. This is also why we call break after handling the input.
         * If a client has been removed the loop variables may not longer be 
         * valid.
         */
        int cur_fd;
        for(cur_fd = 0; cur_fd <= maxfd; cur_fd++) {
            if(FD_ISSET(cur_fd, &rset)) {
                // Check if this socket descriptor is an active player
                for(p = game.head; p != NULL;) {
                    struct client *next = p->next;
                    if(cur_fd == p->fd) {                  
                        int finished_reading = read_from(p, &game, &new_players, 1);                        
                    
                        if(finished_reading){
                            // check whether input from p is valid
                            int is_valid = is_valid_input(p, &game); 
                            if(is_valid == 1){
                                // client's guess is valid, modify game
                                char guess = p->inbuf[0];

                                char* found = strchr(game.word, guess);
                                if(found == NULL){
                                    // if letter does not appear in the word
                                    game.guesses_left -= 1;

                                    // add guess to guess list
                                    for(int i = 0; i < NUM_LETTERS; i++){
                                        if(game.letters_guessed[i] == 0){
                                            game.letters_guessed[i] = guess;
                                            break;
                                        }
                                    }                    

                                    printf("Letter %c is not in the word\n", guess);                

                                    // inform client that guess is incorrect
                                    char msg[MAX_BUF];

                                    strncpy(msg, &guess, 1);
                                    msg[1] = '\0';

                                    char not_in[] = " is not in the word.\r\n";
                                    int len = strlen(not_in);
                                    strncat(msg, not_in, len);

                                    if (write(p->fd, msg, len + 1) != len + 1) {
                                        // there is a problem with socket
                                        help_disconnect(p, &game);
                                    }
                                    process_guess(p, &game, argv[1], 0, guess);                                                                          
                                }
                                else{
                                    // add guess to guess list
                                    for(int i = 0; i < NUM_LETTERS; i++){
                                        if(game.letters_guessed[i] == 0){
                                            game.letters_guessed[i] = guess;
                                            break;
                                        }
                                    }

                                    // uncover letters
                                    for(int i = 0; i < MAX_WORD; i++){
                                        if(game.word[i]){
                                            if(game.word[i] == guess){
                                                game.guess[i] = guess;
                                            }
                                        }
                                        else{
                                            break;
                                        }
                                    }
                                    process_guess(p, &game, argv[1], 1, guess);                                        
                                }
                            }
                        }    
                    }
                    p = next;
                }

                // Check if any new players are entering their names
                for(p = new_players; p != NULL;) {
                    struct client *next = p->next;
                    if(cur_fd == p->fd) {
                        int finished_reading = read_from(p, &game, &new_players, 0);

                        if(finished_reading){
                            int is_valid = 1;
                            if(strlen(p->inbuf) > MAX_NAME - 1){
                                is_valid = 0;
                            }
                            else if(strlen(p->inbuf) >= 1){
                                // check whether any active player has this name
                                for(struct client *q = game.head; q != NULL; q = q->next) {
                                    if(strcmp(q->name, p->inbuf) == 0){
                                        is_valid = 0;
                                        break;
                                    }
                                }    
                            }
                            else{
                                // name is invalid because it is an empty string
                                is_valid = 0;
                            }
                            if(is_valid == 1){
                                // add p to active clients
                                add_to_game(p, &new_players, &game);
                            }
                            else{
                                // send feedback back to client telling them their name is 
                                // invalid
                                char msg[MAX_BUF] = "Unacceptable name. Please enter your name:\r\n";
                                int len = strlen(msg);
                                if (write(p->fd, msg, len) != len) {
                                    // problem with socket
                                    help_disconnect(p, &game);
                                }
                            }
                        }
                    } 
                    p = next;
                }
            }
        }
    }
    return 0;
}
