/****************************************************************************
 * Contest 2026 team 004 - hello app sample
 ****************************************************************************/

#include <stdio.h>
#include <nuttx/nuttx.h>
#include <nuttx/board.h>
#include <unistd.h>
#include <nshlib/nshlib.h>
#include <pthread.h>


static void *nsh_thread(void *arg)
{
    nsh_initialize();
    nsh_consolemain(0, NULL);
    return NULL;
}
int main(int argc, char *argv[])
{
    pthread_t tid;
    pthread_create(&tid, NULL, nsh_thread, NULL);
    uint8_t lednum = board_userled_initialize();
    printf("LED num: %d\r\n", lednum);
    printf("Hello from openvela contest 2026 team 004!\n");
    for(;;)
    {
        usleep(1000000);
        board_userled(1, true);
        printf("LED:ON\r\n");
        usleep(1000000);
        board_userled(1,false);
        printf("LED:OFF\r\n");
    }
    return 0;
    
}
