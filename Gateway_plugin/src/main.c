// Unified main.c for diagnostics (threaded loop) and cellular (run once)
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "../physoftware/dcu_diagnostics/include/dcu_diagnostics_publisher.h"
#include "../physoftware/Gateway-commissioning/include/cellular_network.h"

void *diagnostics_thread(void *arg)
{
    run_diagnostics_publisher();
    return NULL;
}

int main()
{
    pthread_t diag_tid;
    if (pthread_create(&diag_tid, NULL, diagnostics_thread, NULL) != 0)
    {
        fprintf(stderr, "Failed to start diagnostics thread\n");
        return 1;
    }
    run_cellular_network();
    pthread_join(diag_tid, NULL);
    return 0;
}
// TODO: YET TO BE DONE
