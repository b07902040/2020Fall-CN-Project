#include <iostream>
#include <sys/socket.h> 
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h> 
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include "opencv2/opencv.hpp"
#include <signal.h>
#include <pthread.h>

#define BUFF_SIZE 1024
#define Video_buff_size 8192
#define buffer_frame 300
using namespace std;
using namespace cv;

int serverFD;
int buffer_img_size;
int buffer_recv_i, buffer_play_i;
int stop_recv;
int end_frame;
uchar *big_buffer[buffer_frame];

uchar *clone_buffer(uchar *buffer, size_t size)
{
   uchar *clone = new uchar[size];
   memcpy(clone, buffer, size);
   return clone;
}

void close_Socket(int unused) {
    printf("close Socket\n");
    close(serverFD);
    exit(0);
}

void send_file(int sockfd, int filefd){
    char data[1024];
    int filesize = lseek(filefd, 0, SEEK_END);
    lseek(filefd, 0, SEEK_SET);
    int count = filesize/1024;
    send(sockfd, &count, sizeof(int), 0);
    for ( int i = 0; i < count; i++ ) {
        read(filefd, data, 1024);
        if (send(sockfd, data, 1024, 0) == -1) {
            perror("[-]Error in sending file.");
            exit(1);
        }
        bzero(data, 0);
    }
    int remain = filesize-count*1024;
    if ( remain == 0 ) {
        remain = -1;
        send(sockfd, &remain, sizeof(int), 0);
        return;
    }
    send(sockfd, &remain, sizeof(int), 0);
    read(filefd, data, remain);
    send(sockfd, data, remain, 0);
    close(filefd);
    return;
}

void write_file(int sockfd, char *filename){
    int filefd;
    char buffer[1024];
    filefd = open(filename, O_CREAT | O_WRONLY, S_IRWXU | S_IRWXG | S_IRWXO);
    int count;
    recv(sockfd, &count, sizeof(int), 0);
    for ( int i = 0; i < count; i++ ) {
        recv(sockfd, buffer, 1024, 0);
        write(filefd, buffer, 1024);
        bzero(buffer, 0);
    }
    int remain;
    recv(sockfd, &remain, sizeof(int), 0);
    if ( remain == -1 ) return;
    recv(sockfd, buffer, remain, 0);
    write(filefd, buffer, remain);
    close(filefd);
    return;
}

void *video_buffering(void *arg){
    int imgSize = buffer_img_size;
    uchar buffer[imgSize+5], sbuffer[Video_buff_size+5];
    int inst = 1;
    int recv_n, idx;
    buffer_recv_i = 0;
    uchar *iptr;
    while(1){
        if ( buffer_recv_i-buffer_play_i > 250 ) {
            printf("To slow\n");
            sleep(1);
        }
        recv(serverFD, &buffer_img_size, sizeof(int), 0);
        // setsockopt(serverFD,SOL_SOCKET,SO_RCVBUF,(const char*)&imgSize,sizeof(int));
        if ( buffer_img_size == -1 ) {
            end_frame = buffer_recv_i;
            break;
        }
        iptr = buffer;
        idx = 0;
        while ( idx < imgSize ) {
            recv_n = recv(serverFD, sbuffer, min(Video_buff_size, imgSize-idx), 0);
            memcpy(iptr, sbuffer, recv_n);
            iptr += recv_n;
            idx += recv_n;
        }
        //printf("hi\n");
        memcpy(big_buffer[buffer_recv_i%buffer_frame], buffer, imgSize);
        buffer_recv_i++;
        if(stop_recv==1) {
            inst = 2;
            send(serverFD, &inst, sizeof(int), 0);
            break;
        }
        send(serverFD, &inst, sizeof(int), 0);
    }
    pthread_exit(NULL);
}

int main(int argc , char *argv[])
{
    for ( int i = 1; i < 20; i++ ) {
        if ( i != 9 )
            signal(i, close_Socket);
    }
    char sendbuf[BUFF_SIZE] = {};
    char recvbuf[BUFF_SIZE] = {};
    //Create Socket
    serverFD = socket(AF_INET , SOCK_STREAM , 0);
    if (serverFD == -1){
        printf("Fail to create a socket.\n");
        return 0;
    }
    // Prepare socketaddr structure
    struct sockaddr_in info;
    bzero(&info,sizeof(info));
    info.sin_family = PF_INET;
    info.sin_addr.s_addr = inet_addr(argv[1]);
    info.sin_port = htons(atoi(argv[2]));
    // Connect to server
    int err = connect(serverFD,(struct sockaddr *)&info,sizeof(info));
    if(err==-1){
        printf("Connection error\n");
        return 0;
    }
    // Create Folder
    struct stat st = {0};
    int aaa = 1;
    char dirname[1024];
    while (1) {
        char client_data[1024] = {"./client_data_"};
        strcpy(dirname, client_data);
        char c[10];
        sprintf(c, "%d", aaa);
        strcat(dirname, c);
        if ( stat(dirname, &st) == -1 ) {
            mkdir(dirname, 0777);
            break;
            //printf("mkdir success\n");
        }
        bzero(dirname, 1024);
        aaa += 1;
    }
    while(1) {
        fgets(sendbuf, 1024, stdin);
        char sendbuf_copy[1024];
        strcpy(sendbuf_copy, sendbuf);
        char parma[100][1024];
        int num = 0;
        char *token;
        token = strtok(sendbuf_copy, " "); 
        while (token != NULL) {
            strcpy(parma[num], token);
            token = strtok(NULL, " ");
            num++;
        }
        //printf("sendbuf:%s", sendbuf);
        //printf("%d\n", num);
        if ( strncmp(sendbuf, "ls", 2) == 0 ) { // ls request
            sendbuf[strcspn(sendbuf, "\n")] = '\0';
            if ( strlen(sendbuf) != 2 ) {
                printf("Command format error.\n");
                continue;
            }
            send(serverFD, sendbuf, sizeof(sendbuf), 0);
            int n;
            recv(serverFD, &n, sizeof(int), 0);
            int len;
            for ( int i = 2; i < n; i++ ) {
                recv(serverFD, &len, sizeof(int), 0);
                recv(serverFD, recvbuf, len, 0);
                printf("%s\n", recvbuf);
                bzero(recvbuf, BUFF_SIZE);
                len = 0;
            }
        }
        else if ( strncmp(sendbuf, "put", 3) == 0 ) {
            if ( num != 2 ) {
                printf("Command format error.\n");
                continue;
            }
            char filename[100] = {};
            strcpy(filename, dirname);
            strcat(filename, "/");
            strcat(filename, &sendbuf[4]);
            filename[strcspn(filename, "\n")] = '\0';
            //remove(&filename[0], &filename[0]+strlen(filename), '\n') = '\0';
            //chdir("./client_data");
            //printf("filename: %s\n", filename);
            int filefd;
            filefd = open(filename, O_RDONLY);
            //printf("filefd = %d\n", filefd);
            if ( filefd <= 1 ) {
                send(serverFD, "ack", sizeof(char)*3, 0);
                sendbuf[strcspn(sendbuf, "\n")] = '\0';
                printf("The %s doesn't exist\n", &sendbuf[4]);
            }
            else {
                send(serverFD, sendbuf, sizeof(sendbuf), 0);
                send_file(serverFD, filefd);
            }
        }
        else if ( strncmp(sendbuf, "get", 3) == 0 ) {
            if ( num != 2 ) {
                printf("Command format error.\n");
                continue;
            }
            char filename[100] = {};
            strcpy(filename, dirname);
            strcat(filename, "/");
            strcat(filename, &sendbuf[4]);
            filename[strcspn(filename, "\n")] = '\0';
            send(serverFD, sendbuf, sizeof(sendbuf), 0);
            recv(serverFD, recvbuf, sizeof(recvbuf), 0);
            if ( strcmp(recvbuf, "ack") == 0 ) {
                sendbuf[strcspn(sendbuf, "\n")] = '\0';
                printf("The %s doesn't exist\n", &sendbuf[4]);
            }
            else {
                write_file(serverFD, filename);
            }
        }
        else if ( strncmp(sendbuf, "play", 4) == 0 ) {
            stop_recv = 0;
            if ( num != 2 ) {
                printf("Command format error.\n");
                continue;
            }
            sendbuf[strcspn(sendbuf, "\n")] = '\0';
            //printf("%s", &sendbuf[strlen(sendbuf)-4]);
            if ( strncmp(".mpg", &sendbuf[strlen(sendbuf)-4], 4) != 0 ) {
                printf("The %s is not a mpg file\n", &sendbuf[5]);
                continue;
            }
            send(serverFD, sendbuf, sizeof(sendbuf), 0);
            Mat imgClient[buffer_frame];
            int width, height;
            recv(serverFD, &width, sizeof(int), 0);
            if ( width == -1 ) {
                printf("The %s doesn't exist\n", &sendbuf[5]);
                continue;
            }
            recv(serverFD, &height, sizeof(int), 0);
            for ( int i = 0; i < buffer_frame; i++ ) {
                imgClient[i] = Mat::zeros(height,width, CV_8UC3);
                if(!imgClient[i].isContinuous()){
                    imgClient[i] = imgClient[i].clone();
                }
                big_buffer[i] = imgClient[i].data;
            }
            int imgSize = width*height*3;
            buffer_img_size = imgSize;
            buffer_play_i = 0;
            pthread_t pid;
            end_frame = 0;
            int *input = new int(serverFD);
            pthread_create(&pid, NULL, video_buffering, (void*)input);
            printf("start play\n");
            while(1){
                printf("recv: %d play: %d\n", buffer_recv_i, buffer_play_i);
                if ( buffer_recv_i-10 < buffer_play_i && end_frame == 0) {
                    printf("To quick\n");
                    sleep(1);
                }
                imshow("Video", imgClient[buffer_play_i%buffer_frame]);
                buffer_play_i++;
                char c = (char)waitKey(33.3333);
                if ( c==27 ) {
                    stop_recv = 1;
                    break;
                }
                if ( buffer_play_i == end_frame ) {
                    break;
                }
            }
            destroyAllWindows();
            pthread_join(pid, NULL);
        }
        else {
            printf("Command not found.\n");
        }
        bzero(sendbuf, BUFF_SIZE);
        bzero(recvbuf, BUFF_SIZE);
        printf("Ready for next command\n");
    }
    printf("close Socket\n");
    close(serverFD);
    return 0;
}