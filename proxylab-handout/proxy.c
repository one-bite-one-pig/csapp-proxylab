#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";



void doit(int fd); 
void build_complete_request(rio_t *rp,char* buf,char *path, char* method,char*hostname); 
void parse_request_line(char *whole, char *hostname, char *path,char * port ); 



struct cacheline{
    char content[MAX_OBJECT_SIZE];
    int size;
    int time;
    char whole[MAXLINE];

};


struct cache_t{
    struct cacheline line[10];
    int used;
};

struct cache_t cache;
sem_t mutex,w;
int read_cnt=0;

int currenttime=0;

void init_cache(){
    cache.used=0;
    for(int i=0;i<10;i++){
        cache.line[i].size=0;
        cache.line[i].time=-1;
    }
    sem_init(&mutex, 0, 1);
    sem_init(&w, 0, 1);
}



int cache_read(char* whole){
    int result=-1;

    P(&mutex);
    read_cnt++;

    if(read_cnt==1){
        P(&w);
    }

    V(&mutex);

    for(int i=0;i<cache.used;i++){
        if(!strcmp(cache.line[i].whole,whole)){
            P(&mutex);
            cache.line[i].time=currenttime;
            V(&mutex);
            result=i;
            return result;
        }
    }


    P(&mutex);
    read_cnt--;

    if (read_cnt == 0) {
        V(&w);
    }
    V(&mutex);

    return result;

}


void cache_write(char* whole, char* content, int content_size){
    P(&w);
    if (cache.used==10){
        int oldest=0;
        int oldtime=cache.line[0].time;
        for(int i=1;i<10;i++){
            if(cache.line[i].time<oldtime){
                oldest=i;
                oldtime=cache.line[i].time;
            }
        }
        P(&mutex);
        strcpy(cache.line[oldest].whole,whole);
        strcpy(cache.line[oldest].content,content);
        cache.line[oldest].size=content_size;
        cache.line[oldest].time=currenttime;
        V(&mutex);
    }
    else{
        P(&mutex);
        cache.used++;
        strcpy(cache.line[cache.used-1].whole,whole);
        strcpy(cache.line[cache.used-1].content,content);
        cache.line[cache.used-1].size=content_size;
        cache.line[cache.used-1].time=currenttime;
        V(&mutex);
    }
    V(&w);






}







void sighandler(int sig)
{
    ;
}

void* thread(void * vargp){
    int connfd=*((int*)vargp);
    Pthread_detach(Pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}



int main(int argc, char **argv) 
{ 
    int listenfd;
    sem_t mutex;
    int* connfdp; 
    pthread_t tid;
    char hostname[MAXLINE], port[MAXLINE]; 
    socklen_t clientlen; 
    struct sockaddr_storage clientaddr; 

    Signal(SIGPIPE, sighandler);

    init_cache();

    if (argc != 2) { 
        fprintf (stderr, "usage: %s <port>\n", argv [0]); 
        exit(1); 
    } 

    listenfd = Open_listenfd(argv [1]); 

    while (1) { 
        clientlen = sizeof(clientaddr); 

        connfdp = (int*)malloc(sizeof(int));

        *connfdp=Accept(listenfd, (SA *)&clientaddr, &clientlen); 

        currenttime++;

        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port , MAXLINE,0) ; 
        printf("Accepted connection from (%s, %s)\n", hostname, port); 

        Pthread_create(&tid,NULL,thread,(void*)connfdp);
         
    }
}








void doit(int fd) 
{
    /* handle the request     */
    char head[MAXLINE], method[MAXLINE],hostname[MAXLINE],path[MAXLINE],port[MAXLINE], whole[MAXLINE], version[MAXLINE];

    char buf[MAXLINE];
 
    rio_t rio;

    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE))  
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, whole, version);

    int temp=cache_read(whole);
    
    if(temp!=-1){
        Rio_writen(fd,cache.line[temp].content,cache.line[temp].size);
        return;
    }
    
                                                   
    parse_request_line(whole,hostname,path,port);

    build_complete_request(&rio,head,path,method,hostname);


        /*send request to the server*/
    int serverfd=Open_clientfd(hostname,port);

    Rio_writen(serverfd,head,strlen(head));


        /*foward */
    int n;
    rio_t server_rio;

    Rio_readinitb(&server_rio, serverfd);

    int total=0;
    char file[MAX_OBJECT_SIZE];


    while ((n = Rio_readlineb(&server_rio, buf, MAXLINE))) {
        Rio_writen(fd, buf, n);
        strcat(file,buf);
        total+=n;
        
    }
    if(total<=MAX_OBJECT_SIZE){
        cache_write(whole,file,total);
    }



    close(serverfd);
    
}




void build_complete_request(rio_t *rp,char* head,char *path, char* method,char *hostname) 
{
    char buf[MAXLINE];  
    int has_host_flag = 0;

    sprintf(head,"GET %s HTTP/1.0\r\n",path);

    while (1) {
        rio_readlineb(rp, buf, MAXLINE);
        if (strcmp(buf, "\r\n") == 0) {
            break;
        }
        if (!strncasecmp(buf, "Host:", strlen("Host:"))) {
            has_host_flag = 1;
        }
        if (!strncasecmp(buf, "Connection:", strlen("Connection:"))) {
            continue;
        }
        if (!strncasecmp(buf, "Proxy-Connection:", strlen("Proxy-Connection:"))) {
            continue;
        }
        if (!strncasecmp(buf, "User-Agent:", strlen("User-Agent:"))) {
            continue;
        }
        strcat(head, buf);
    }

    if (!has_host_flag) {
        sprintf(buf+strlen(buf), "Host: %s\r\n", head,hostname);
        strcpy(head, buf);
    }

    strcat(head, "Connection: close\r\n");
    strcat(head, "Proxy-Connection: close\r\n");
    strcat(head, user_agent_hdr);
    strcat(head, "\r\n");

}



void parse_request_line(char *whole, char *hostname, char *path,char * port) 
{
    char *host_start;
    host_start=strstr(whole,"://");
    host_start+=3;

    char *path_start;
    path_start=strchr(host_start,'/');

    int host_len=path_start-host_start;
    strncpy(hostname,host_start,host_len);
    hostname[host_len]='\0';

    strcpy(path,path_start);

    char *port_start;
    port_start=strchr(host_start,':');

    if(port_start!=NULL){
        char temp[MAXLINE];
        strncpy(temp,hostname,port_start-host_start);
        temp[port_start-host_start]='\0';
        strcpy(hostname,temp);
        strncpy(port, port_start+1,path_start-port_start-1);
        port[path_start-port_start-1]='\0';
    }
    else{
        strcpy(port,"80");
    }
}