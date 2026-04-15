// Claw Runtime - Event System Implementation
// Supports Publish/Subscribe pattern for Claw language

#ifndef CLAW_RUNTIME_EVENT_H
#define CLAW_RUNTIME_EVENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Event Types
//=============================================================================

#define CLAW_MAX_EVENTS 256
#define CLAW_MAX_SUBSCRIBERS_PER_EVENT 32
#define CLAW_MAX_PROCESSES 64

typedef void (*claw_event_handler_t)(void*);

// Event descriptor
typedef struct {
    const char* name;
    claw_event_handler_t handlers[CLAW_MAX_SUBSCRIBERS_PER_EVENT];
    int handler_count;
    void* user_data[CLAW_MAX_SUBSCRIBERS_PER_EVENT];
} claw_event_t;

// Event system global state
typedef struct {
    claw_event_t events[CLAW_MAX_EVENTS];
    int event_count;
    bool initialized;
} claw_event_system_t;

// Get global event system
static claw_event_system_t* claw_get_event_system(void) {
    static claw_event_system_t system = {0};
    if (!system.initialized) {
        system.initialized = true;
    }
    return &system;
}

//=============================================================================
// Event System API
//=============================================================================

// Find or create event by name
static claw_event_t* claw_event_get_or_create(const char* event_name) {
    claw_event_system_t* sys = claw_get_event_system();
    
    // Search existing events
    for (int i = 0; i < sys->event_count; i++) {
        if (strcmp(sys->events[i].name, event_name) == 0) {
            return &sys->events[i];
        }
    }
    
    // Create new event if not found
    if (sys->event_count < CLAW_MAX_EVENTS) {
        claw_event_t* evt = &sys->events[sys->event_count++];
        evt->name = event_name;
        evt->handler_count = 0;
        memset(evt->handlers, 0, sizeof(evt->handlers));
        memset(evt->user_data, 0, sizeof(evt->user_data));
        return evt;
    }
    
    fprintf(stderr, "CLAW Runtime: Too many events (max %d)\n", CLAW_MAX_EVENTS);
    return NULL;
}

// Subscribe to an event
static int claw_event_subscribe(const char* event_name, 
                                 claw_event_handler_t handler,
                                 void* user_data) {
    claw_event_t* evt = claw_event_get_or_create(event_name);
    if (!evt) return -1;
    
    if (evt->handler_count >= CLAW_MAX_SUBSCRIBERS_PER_EVENT) {
        fprintf(stderr, "CLAW Runtime: Too many subscribers for event '%s' (max %d)\n",
                event_name, CLAW_MAX_SUBSCRIBERS_PER_EVENT);
        return -1;
    }
    
    evt->handlers[evt->handler_count] = handler;
    evt->user_data[evt->handler_count] = user_data;
    evt->handler_count++;
    
    printf("[CLAW] SUBSCRIBED: %s -> handler_%p\n", event_name, (void*)handler);
    return evt->handler_count - 1;
}

// Dispatch an event
static void claw_event_dispatch(const char* event_name, void* event_data) {
    claw_event_t* evt = claw_event_get_or_create(event_name);
    if (!evt) {
        fprintf(stderr, "CLAW Runtime: Event '%s' not found\n", event_name);
        return;
    }
    
    printf("[CLAW] EVENT: %s\n", event_name);
    
    // Call all handlers
    for (int i = 0; i < evt->handler_count; i++) {
        if (evt->handlers[i]) {
            evt->handlers[i](evt->user_data[i] ? evt->user_data[i] : event_data);
        }
    }
}

// Unsubscribe from an event
static int claw_event_unsubscribe(const char* event_name, int subscriber_id) {
    claw_event_t* evt = claw_event_get_or_create(event_name);
    if (!evt || subscriber_id < 0 || subscriber_id >= evt->handler_count) {
        return -1;
    }
    
    evt->handlers[subscriber_id] = NULL;
    evt->user_data[subscriber_id] = NULL;
    return 0;
}

//=============================================================================
// Serial Process API
//=============================================================================

// Process descriptor
typedef struct {
    const char* name;
    int argc;
    char** argv;
    void (*main)(int argc, char** argv);
    bool initialized;
} claw_process_t;

// Process registry
static claw_process_t claw_processes[CLAW_MAX_PROCESSES];
static int claw_process_count = 0;

// Register a process
static int claw_process_init(const char* process_name, int argc, void (*main)(int, char**)) {
    if (claw_process_count >= CLAW_MAX_PROCESSES) {
        fprintf(stderr, "CLAW Runtime: Too many processes (max %d)\n", CLAW_MAX_PROCESSES);
        return -1;
    }
    
    claw_process_t* proc = &claw_processes[claw_process_count++];
    proc->name = process_name;
    proc->argc = argc;
    proc->main = main;
    proc->initialized = true;
    
    printf("[CLAW] PROCESS_INIT: %s (argc=%d)\n", process_name, argc);
    return claw_process_count - 1;
}

// Run a process
static void claw_process_run(int process_id) {
    if (process_id < 0 || process_id >= claw_process_count) {
        fprintf(stderr, "CLAW Runtime: Invalid process id %d\n", process_id);
        return;
    }
    
    claw_process_t* proc = &claw_processes[process_id];
    if (!proc->initialized || !proc->main) {
        fprintf(stderr, "CLAW Runtime: Process '%s' not initialized\n", proc->name);
        return;
    }
    
    printf("[CLAW] Running process: %s\n", proc->name);
    proc->main(proc->argc, NULL);
}

//=============================================================================
// Utility Functions
//=============================================================================

// Print event system status
static void claw_event_print_status(void) {
    claw_event_system_t* sys = claw_get_event_system();
    printf("\n=== CLAW Event System Status ===\n");
    printf("Total events: %d / %d\n", sys->event_count, CLAW_MAX_EVENTS);
    
    for (int i = 0; i < sys->event_count; i++) {
        claw_event_t* evt = &sys->events[i];
        printf("  - %s: %d handlers\n", evt->name, evt->handler_count);
    }
    printf("================================\n\n");
}

// Initialize runtime
static void claw_runtime_init(void) {
    claw_get_event_system();
    printf("[CLAW] Runtime initialized\n");
}

#ifdef __cplusplus
}
#endif

#endif // CLAW_RUNTIME_EVENT_H
