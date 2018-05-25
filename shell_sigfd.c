/*執行方法：
./myShell
>> ls -R /
ctr-c
*/
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/signalfd.h>
#include <sys/epoll.h>
#include <assert.h>
#include <time.h>
///color///
#define NONE "\033[m"
#define RED "\033[0;32;31m"
#define LIGHT_RED "\033[1;31m"
#define GREEN "\033[0;32;32m"
#define LIGHT_GREEN "\033[1;32m"
#define BLUE "\033[0;32;34m"
#define LIGHT_BLUE "\033[1;34m"
#define DARY_GRAY "\033[1;30m"
#define CYAN "\033[0;36m"
#define LIGHT_CYAN "\033[1;36m"
#define PURPLE "\033[0;35m"
#define LIGHT_PURPLE "\033[1;35m"
#define BROWN "\033[0;33m"
#define YELLOW "\033[1;33m"
#define LIGHT_GRAY "\033[0;37m"
#define WHITE "\033[1;37m"
#define RED_BOLD "\x1b[;31;1m"
#define BLU_BOLD "\x1b[;34;1m"
#define YEL_BOLD "\x1b[;33;1m"
#define GRN_BOLD "\x1b[;32;1m"
#define CYAN_BOLD_ITALIC "\x1b[;36;1;3m"
#define RESET "\x1b[0;m"

/*
全域變數，放解析過後的使用者指令（字串陣列）
*/
char* argVect[256];

/*
parseString：將使用者傳進的命令轉換成字串陣列
str：使用者傳進的命令
cmd：回傳執行檔
*/
void parseString(char* str, char** cmd) {
    int idx=0;
    char* retPtr;
    //printf("%s\n", str);
    retPtr=strtok(str, " \n");
    while(retPtr != NULL) {
        //printf("token =%s\n", retPtr);
        //if(strlen(retPtr)==0) continue;
        argVect[idx++] = retPtr;
        if (idx==1)
            *cmd = retPtr;
        retPtr=strtok(NULL, " \n");
    }
    argVect[idx]=NULL;
}

int setupSignalfd() {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigfillset(&sigset);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGCHLD);
    sigprocmask(SIG_BLOCK, &sigset, NULL);
    return signalfd(-1, &sigset, 0);//SFD_CLOEXEC);
}

void printPrompt() {
    char cwd[256], hostName[256];
    char* showPath, *loginName;
    int homeLen = 0, pos = 0;
    
    //抓取主機名稱、用戶名稱
    loginName = getlogin();
    gethostname(hostName, 256);
    //底下程式碼製造要顯示的路徑字串，會參考"home"將"home"路徑取代為~
    getcwd(cwd, 256);
    pos=strspn(getenv("HOME"), cwd);
    homeLen = strlen(getenv("HOME"));
    //printf("cwd=%s, home=%s, pos=%d, prompt=%s", cwd, getenv("HOME"), pos, &cwd[pos]);
    if(pos>=homeLen) {
        cwd[pos-1]='~';
        showPath=&cwd[pos-1];
    }
    else showPath=cwd;
    //底下程式碼負責印出提示符號
    printf(LIGHT_GREEN"%s@%s:"BLU_BOLD"%s>> " NONE, loginName, hostName, showPath);
    fflush(stdout);
}

int main (int argc, char** argv) {
    char cmdLine[4096];
    char* exeName;
    //for child
    int child_pid=-1, wstatus;
    struct timespec startTime, endTime;
    //for signal_fd
    int epollfd, sig_fd;
    struct epoll_event ev, event;
    struct signalfd_siginfo fdsi;

    printf("pid is %d\n", getpid());
    //sleep(10);       //for debugging

    //setup epoll()
    sig_fd=setupSignalfd();
    epollfd = epoll_create1(0);
    ev.events = EPOLLIN;
    ev.data.fd = STDIN_FILENO;  //聽鍵盤
    assert(epoll_ctl(epollfd, EPOLL_CTL_ADD, 1, &ev)!=-1);
    ev.data.fd = sig_fd;    //聽signal
    assert(epoll_ctl(epollfd, EPOLL_CTL_ADD, sig_fd, &ev)!=-1);

    /*無窮迴圈直到使用者輸入exit*/
    while(1) {
        if (child_pid == -1)
            printPrompt();
wait_event:       
        assert(epoll_wait(epollfd, &event, 1, -1) !=-1);
        if (event.data.fd == STDIN_FILENO) {   //from stdin
            int ret = read(STDIN_FILENO, cmdLine, 4096);
            //printf("%s, size=%d\n", cmdLine, ret);
            //printf("\n");
            cmdLine[ret-1]='\0';
            if (child_pid > 0) goto wait_event;
        } else if(event.data.fd == sig_fd) {    //收到signal
            //printf("sig_fd\n");
            memset(&fdsi, 0, sizeof(struct signalfd_siginfo));
            int s = read(sig_fd, &fdsi, sizeof(struct signalfd_siginfo));
            assert(s==sizeof(struct signalfd_siginfo));
            switch(fdsi.ssi_signo) {    //判斷signal number
                case SIGINT:
                    //printf(RED"killchild\n\n\n\n");
                    if (child_pid > 0) {
                        //printf("kill child\n");
                        //system("touch kill_child");
                        int ret=kill(child_pid, fdsi.ssi_signo);
                        if (ret == -1) {
                            //system("touch kill_fault");
                            perror("kill child");
                            exit(1);
                        }
                        child_pid = -1;
                        goto wait_event;
                    } else {
                        printf("\n");
                    }
                    break;
                case SIGCHLD:   //假設child結束
                    //printf("SIGCHLD\n");
                    clock_gettime(CLOCK_REALTIME, &endTime);
                    printf(RED"real: "YELLOW"%ld.%ldsec\n", (endTime.tv_nsec - startTime.tv_nsec)/1000000000, 
                        (endTime.tv_nsec - startTime.tv_nsec)%1000000000);
                    printf(RED"user: "YELLOW"%ld\n", fdsi.ssi_utime);
                    printf(RED"sys : "YELLOW"%ld\n", fdsi.ssi_stime);                    
                    
                    printf(RED "return value of " YELLOW "%s" RED " is " YELLOW "%d\n", 
                        exeName, WEXITSTATUS(fdsi.ssi_status));
                    //printf("isSignaled? %d\n", WIFSIGNALED(wstatus));
                    if (WIFSIGNALED(wstatus))
                        printf(RED"the child process was terminated by a signal "YELLOW"%d"RED
                            ", named " YELLOW "%s.\n",  WTERMSIG(fdsi.ssi_status), sys_siglist[WTERMSIG(fdsi.ssi_status)]);

                    printf(NONE);
                    child_pid = -1;
                    //goto wait_event;
                    break;
                default:
                    printf(RED"signal # is %d\n", fdsi.ssi_signo);

            }
            continue;
        }
        //printf("cmdLine = %s\n",cmdLine);
        if (strlen(cmdLine)>1)  //判斷長度是否大於1，判斷「使用者無聊按下enter鍵」
            parseString(cmdLine, &exeName);
        else
            continue;
        if (strcmp(exeName, "exit") == 0)   //內建指令exit
            break;
        if (strcmp(exeName, "cd") == 0) {   //內建指令cd
            if (strcmp(argVect[1], "~")==0)
                chdir(getenv("HOME"));
            else
                chdir(argVect[1]);
            continue;
        }
        child_pid = vfork();   //除了exit, cd，其餘為外部指令
        if (child_pid == 0) {
            clock_gettime(CLOCK_REALTIME, &startTime);
            //要記得打開signal的遮罩，因為execve會延續這個遮罩
            sigset_t sigset;
            sigfillset(&sigset);
            sigprocmask(SIG_UNBLOCK, &sigset, NULL);
            //產生一個child執行使用者的指令
            if (execvp(exeName, argVect)==-1) {
                perror("myShell");
                exit(errno*-1);
            }
        }
    }
}