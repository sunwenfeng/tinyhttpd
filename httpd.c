/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <winsock.h>

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

void accept_request(int);//
void bad_request(int);

void cat(int, FILE *);

void cannot_execute(int);

void error_die(const char *);

void execute_cgi(int, const char *, const char *, const char *);

int get_line(int, char *, int);

void headers(int, const char *);

void not_found(int);

void serve_file(int, const char *);

int startup(u_short *);

void unimplemented(int);

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
void accept_request(int client) {
    char buf[1024];
    int numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0;      /* becomes true if server decides this is a CGI
                    * program */
    char *query_string = NULL;

    numchars = get_line(client, buf, sizeof(buf));
    i = 0;
    j = 0;
    while (!ISspace(buf[j]) && (i < sizeof(method) - 1)) {
        method[i] = buf[j];
        i++;
        j++;
    }
    method[i] = '\0';

    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))//忽略大小写比较字符串，linux平台，相等返回0，如果都不是
    {
        unimplemented(client);
        return;
    }

    if (strcasecmp(method, "POST") == 0)//
        cgi = 1;

    i = 0;
    while (ISspace(buf[j]) && (j < sizeof(buf)))
        j++;
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf))) {
        url[i] = buf[j];
        i++;
        j++;
    }
    url[i] = '\0';

    if (strcasecmp(method, "GET") == 0) {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))//解析地址
            query_string++;
        if (*query_string == '?')//如果有？，表明是动态服务
        {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }

    sprintf(path, "htdocs%s", url);//请求的默认目录在htdocs中，在url前加htdocs表示完整路径
    //没有指定任何服务，则默认打开index.html
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");
    if (stat(path, &st) == -1) {//stat通过path获取文件信息，失败则返回-1，成功返回0。
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));//找不到对应的服务之后，从socket中把所有的请求头部读出来，并且丢弃
        not_found(client);//向客户端发送not found错误
    } else {
        if ((st.st_mode & S_IFMT) == S_IFDIR)//文件类型是目录，默认执行index.html
            strcat(path, "/index.html");
        if ((st.st_mode & S_IXUSR) ||
            (st.st_mode & S_IXGRP) ||
            (st.st_mode & S_IXOTH))//该文件具有执行权限
            cgi = 1;
        if (!cgi)//不是动态服务
            serve_file(client, path);//直接把服务器文件返回
        else
            execute_cgi(client, path, method, query_string);//否则执行cgi
    }

    close(client);
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client) {
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource) {
    char buf[1024];

    fgets(buf, sizeof(buf), resource);
    while (!feof(resource)) {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client) {
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc) {
    perror(sc);
    exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path,
                 const char *method, const char *query_string) {
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A';
    buf[1] = '\0';
    if (strcasecmp(method, "GET") == 0)//如果是get方法则读取请求头部并丢弃
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
    else    /* POST */
    {
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf)) {
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0)//如果是post方法，从请求头部读取出content-length
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
        if (content_length == -1) {//没有读取到content_length
            bad_request(client);
            return;
        }
    }

    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);//返回响应状态行

    if (pipe(cgi_output) < 0) {//创建一个管道用于cgi输出
        cannot_execute(client);
        return;
    }
    if (pipe(cgi_input) < 0) {//创建一个管道用于cgi输入
        cannot_execute(client);
        return;
    }

    if ((pid = fork()) < 0) {
        cannot_execute(client);
        return;
    }
    if (pid == 0)  /* child: CGI script */
    {/*
 * pipe是半双工管道，fd[0]用于读，fd[1]用于写
 *
 *
 *
 * */
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        /*
         * dup2用于重定向描述符dup2(int oldfd,int targetfd)
         *将oldfd设为targetfd
         * */

        dup2(cgi_output[1], 1);//重定向标准输出到管道的输入，也就是将原本输出到标准输出的内容输出到管道output
        dup2(cgi_input[0], 0);//重定向标准输入到管道的输出，把从管道input的读到的数据发送给标准输入0
        close(cgi_output[0]);//子进程用于输出，关闭输出管道的读端
        close(cgi_input[1]);//关闭输入管道的写端
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if (strcasecmp(method, "GET") == 0) {
            sprintf(query_env, "QUERY_STRING=%s", query_string);//设置环境变量，putenv(value)表示将字符串value加到当前环境中
            putenv(query_env);
        } else {   /* POST */
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        execl(path, path, NULL);
        /* 之所以要用重定向，是因为execl的输入和输出分别是标准输入和顺出，但在这个地方execl的输入是父进程通过管道发给它的。输出也要通过管道发送给父进程。
         * 调用execl执行path指定的程序，参数为path，环境为null，由于dup2的重定向，execl的输出进入输出管道
        */exit(0);
    } else {    /* parent */
        close(cgi_output[1]);//父进程关闭输出管道的写端
        close(cgi_input[0]);//子进程关闭输入管道的读端
        if (strcasecmp(method, "POST") == 0)
            for (i = 0; i < content_length; i++) {
                recv(client, &c, 1, 0);//每次读一个字符
                write(cgi_input[1], &c, 1);//然后写入到输入管道
            }
        while (read(cgi_output[0], &c, 1) > 0)//cgi执行就将结果输出到output管道。读取output管道的输出值，并发送给客户端
            send(client, &c, 1, 0);

        close(cgi_output[0]);//关闭管道
        close(cgi_input[1]);
        waitpid(pid, &status, 0);//等待子进程关闭后再关闭，
    }
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size) {//buf和buf的长度
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n')) {
        n = recv(sock, &c, 1, 0);//每次只读一个字节
        /* DEBUG printf("%02X\n", c); */
        if (n > 0) {//  \r\n为换行符
            if (c == '\r') {//遇见\r之后继续读一个字符
                n = recv(sock, &c, 1, MSG_PEEK);//使用MSG_PEEK，表示下次读取仍然可以读到这次读取的数据
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }//总之，上面if代码段读到\n
            buf[i] = c;
            i++;
        } else
            c = '\n';//没有字符之后字符末尾加上\n
    }
    buf[i] = '\0';
    return (i);
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int client, const char *filename) {
    char buf[1024];
    (void) filename;  /* could use filename to determine file type */

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int client) {
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int client, const char *filename) {
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A';
    buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers，读取并丢弃所有的头部信息 */
        numchars = get_line(client, buf, sizeof(buf));

    resource = fopen(filename, "r");//打开文件
    if (resource == NULL)
        not_found(client);//如果文件不存在，返回错误
    else {
        headers(client, filename);//返回响应报文的状态行和消息包头
        cat(client, resource);//每次从文件中读取一行并返回给客户端
    }
    fclose(resource);
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int startup(u_short *port)//定义套接字描述符
{
    int httpd = 0;
    struct sockaddr_in name;

    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("socket");
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);//服务端应该指定有一个周知端口，如http服务的80
    name.sin_addr.s_addr = htonl(INADDR_ANY);//通配地址，bind时由内核选择IP地址
    if (bind(httpd, (struct sockaddr *) &name, sizeof(name)) < 0)
        error_die("bind");
    if (*port == 0)  /* if dynamically allocating a port *  端口为0同样指示bind时由内核指定端口*/
    {
        int namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr *) &name, &namelen) == -1)//因为是由内核指定ip和端口，为了获取端口，调用getsockname
            error_die("getsockname");
        *port = ntohs(name.sin_port);
    }
    if (listen(httpd, 5) < 0)
        error_die("listen");
    return (httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client) {
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void) {
    int server_sock = -1;
    u_short port = 0;
    int client_sock = -1;
    struct sockaddr_in client_name;
    int client_name_len = sizeof(client_name);
    pthread_t newthread;

    server_sock = startup(&port);
    printf("httpd running on port %d\n", port);

    while (1) {
        client_sock = accept(server_sock,
                             (struct sockaddr *) &client_name,
                             &client_name_len);
        if (client_sock == -1)
            error_die("accept");
        /* accept_request(client_sock); */
        if (pthread_create(&newthread, NULL, accept_request, client_sock) != 0)
            perror("pthread_create");
    }

    close(server_sock);

    return (0);
}
