# Claw Event-Driven Architecture

## Core Philosophy: Everything is a Process or Event Stream

All computation in Claw is modeled as either:
- **Serial Processes**: Sequential execution with early termination
- **Parallel Processes**: Concurrent execution with collective completion

## Process Declaration Syntax

### Serial Process (Chain Processing)
```claw
serial process validate_user(name input: byte[]) -> bool {
    // Processors are called in order
    // Return true to stop the chain, false to continue
}
```

### Parallel Process (Fan-out Processing)  
```claw
parallel process notify_all(name event: byte[]) -> Result<(), Error> {
    // All subscribers receive the event simultaneously
    // Completion determined by subscriber responses
}
```

## Publisher-Subscriber Model

### Publishing Events
```claw
// Publish to serial process
publish validate_user(user_data)

// Publish to parallel process  
publish notify_all(system_event)
```

### Subscribing to Events
```claw
// Subscribe to serial process (order matters)
subscribe validate_user with password_check
subscribe validate_user with email_verify  
subscribe validate_user with rate_limit

// Subscribe to parallel process (order doesn't matter)
subscribe notify_all with send_email
subscribe notify_all with send_sms
subscribe notify_all with log_event
```

## Serial Process Behavior

1. **Ordered Execution**: Subscribers called in subscription order
2. **Early Termination**: Any subscriber returning `true` stops the chain
3. **State Propagation**: Each subscriber can modify the event data
4. **Error Handling**: Exceptions propagate to the publisher

### Serial Example: Authentication Pipeline
```claw
serial process authenticate(name credentials: byte[]) -> bool

subscribe authenticate with check_format {
    if !valid_format(credentials) {
        return true  // Stop here, invalid format
    }
    return false // Continue to next subscriber
}

subscribe authenticate with verify_password {
    if !password_correct(credentials) {
        return true  // Stop here, wrong password  
    }
    return false // Continue
}

subscribe authenticate with check_ban_list {
    if user_banned(credentials) {
        return true  // Stop, user banned
    }
    return false // Success, continue
}

// Usage
if publish authenticate(login_data) {
    println("Authentication failed")
} else {
    println("Authentication succeeded") 
}
```

## Parallel Process Behavior

1. **Simultaneous Delivery**: All subscribers receive event at once
2. **Response Collection**: Publisher waits for all subscriber responses
3. **Completion Logic**: Configurable success/failure criteria
4. **Timeout Handling**: Automatic cleanup on timeout

### Parallel Example: Notification System
```claw
parallel process alert_admin(name alert: byte[]) -> Result<(), Error>

subscribe alert_admin with email_notification {
    send_email("admin@company.com", alert.text())
    return Ok(())  // Always succeeds
}

subscribe alert_admin with sms_notification {
    if send_sms("+1234567890", alert.summary()) {
        return Ok(())
    } else {
        return Err(SmsFailed)  // SMS might fail
    }
}

subscribe alert_admin with log_to_file {
    file("/var/log/alerts.log").append(alert)
    return Ok(())
}

// Usage - waits for all subscribers
match publish alert_admin(critical_error) {
    Ok(_) => println("All notifications sent"),
    Err(e) => println("Some notifications failed: {}", e)
}
```

## Process Composition

### Nested Processes
```claw
serial process handle_request(name req: byte[]) -> bool {
    // Can publish to other processes
    if publish authenticate(req) {
        return true  // Auth failed
    }
    
    if publish authorize(req) {
        return true  // Authz failed  
    }
    
    publish process_request(req)  // Parallel processing
    return false
}
```

### Conditional Publishing
```claw
fn route_event(name event: byte[]) {
    match event.type() {
        "user" => publish handle_user(event),
        "system" => publish handle_system(event), 
        "network" => publish handle_network(event),
        _ => publish handle_unknown(event)
    }
}
```

## Memory and Resource Management

### Event Data Ownership
- Serial processes: Shared mutable access (with borrow checking)
- Parallel processes: Immutable shared access or owned copies
- Compiler ensures memory safety across process boundaries

### Resource Cleanup
- Automatic cleanup when process completes
- Manual cleanup available via `unsafe` blocks
- Resource leaks prevented by ownership system

## Performance Characteristics

### Serial Processes
- **Latency**: O(n) where n = number of subscribers
- **Memory**: O(1) additional memory per process
- **Use cases**: Validation chains, middleware pipelines

### Parallel Processes  
- **Latency**: O(max(subscriber_latency))
- **Memory**: O(n) where n = number of subscribers
- **Use cases**: Notifications, fan-out operations, distributed processing

## Error Handling Strategies

### Serial Error Propagation
- First error stops the chain
- Error context preserved for debugging
- Recovery possible via try-catch at publisher level

### Parallel Error Aggregation  
- All errors collected and returned
- Partial success possible (some succeed, some fail)
- Configurable failure thresholds

## Integration with Hardware and System Events

### Hardware Interrupts as Events
```claw
// GPIO interrupt becomes a parallel process
name gpio_interrupt = hardware_event("gpio_13")

subscribe gpio_interrupt with led_toggle
subscribe gpio_interrupt with log_interrupt  
subscribe gpio_interrupt with send_alert
```

### System Signals as Events
```claw
// SIGTERM becomes a serial process
serial process shutdown_handler(name signal: byte[])

subscribe shutdown_handler with close_connections
subscribe shutdown_handler with flush_logs  
subscribe shutdown_handler with exit_gracefully
```

This event-driven architecture provides a unified model for all computation in Claw, from low-level hardware interrupts to high-level application logic, all while maintaining the simplicity and safety that defines the language.