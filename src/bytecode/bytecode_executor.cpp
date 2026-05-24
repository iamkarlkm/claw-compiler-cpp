// BytecodeExecutor.cpp - 字节码执行器实现

#include "bytecode_executor.h"
#include "../lexer/lexer.h"
#include "../parser/parser.h"

namespace claw {

// ============================================================================
// BytecodeExecutor 实现
// ============================================================================

BytecodeExecutionResult BytecodeExecutor::execute(
    std::shared_ptr<ast::Program> program, 
    bool verbose) {
    
    BytecodeExecutionResult result;
    
    if (!program) {
        result.error_message = "Null program pointer";
        return result;
    }
    
    if (verbose) {
        std::cout << "[BytecodeExecutor] Starting bytecode execution...\n";
    }
    
    // 编译阶段
    auto compile_start = std::chrono::high_resolution_clock::now();
    auto module = compile_to_bytecode(program, result);
    auto compile_end = std::chrono::high_resolution_clock::now();
    result.stats.compile_time = std::chrono::duration_cast<std::chrono::microseconds>(
        compile_end - compile_start);
    
    if (!module) {
        // compile_to_bytecode 已经设置 error_message
        return result;
    }
    
    // 统计
    result.stats.functions_compiled = module->functions.size();
    for (const auto& func : module->functions) {
        result.stats.bytecode_instructions += func.code.size();
    }
    
    // 执行阶段
    auto exec_start = std::chrono::high_resolution_clock::now();
    result.return_value = execute_in_vm(*module, result);
    auto exec_end = std::chrono::high_resolution_clock::now();
    result.stats.execution_time = std::chrono::duration_cast<std::chrono::microseconds>(
        exec_end - exec_start);
    
    result.success = true;
    
    if (verbose) {
        std::cout << "[BytecodeExecutor] Execution completed successfully\n";
        std::cout << "  Compile time: " << result.stats.compile_time.count() << " us\n";
        std::cout << "  Execution time: " << result.stats.execution_time.count() << " us\n";
        std::cout << "  Functions: " << result.stats.functions_compiled << "\n";
        std::cout << "  Bytecode instructions: " << result.stats.bytecode_instructions << "\n";
    }
    
    return result;
}

BytecodeExecutionResult BytecodeExecutor::execute_from_source(
    const std::string& source, 
    bool verbose) {
    
    BytecodeExecutionResult result;
    
    if (source.empty()) {
        result.error_message = "Empty source code";
        return result;
    }
    
    if (verbose) {
        std::cout << "[BytecodeExecutor] Loading source (" << source.size() << " bytes)\n";
    }
    
    // 词法分析
    Lexer lexer(source);
    auto tokens = lexer.scan_all();
    
    if (verbose) {
        std::cout << "[BytecodeExecutor] Lexed " << tokens.size() << " tokens\n";
    }
    
    // 语法分析
    Parser parser(tokens);
    auto program = parser.parse();
    
    if (!program) {
        result.error_message = "Parse failed";
        return result;
    }
    
    if (verbose) {
        std::cout << "[BytecodeExecutor] Parsed " << program->get_declarations().size() 
                  << " declarations\n";
    }
    
    // 执行编译和运行
    return execute(std::shared_ptr<ast::Program>(std::move(program)), verbose);
}

std::shared_ptr<bytecode::Module> BytecodeExecutor::compile_to_bytecode(
    std::shared_ptr<ast::Program> program, 
    BytecodeExecutionResult& result) {
    
    try {
        // 创建编译器
        BytecodeCompiler compiler;
        compiler.setDebugInfo(debug_);
        
        // 编译
        auto module = compiler.compile(*program);
        
        if (!module) {
            result.error_message = "Compilation failed: " + compiler.getLastError();
            return nullptr;
        }
        
        if (debug_) {
            std::cout << "[BytecodeExecutor] Compiled " << module->functions.size() 
                      << " functions\n";
        }
        
        return module;
        
    } catch (const std::exception& e) {
        result.error_message = std::string("Compilation exception: ") + e.what();
        return nullptr;
    }
}

vm::Value BytecodeExecutor::execute_in_vm(
    const bytecode::Module& module,
    BytecodeExecutionResult& result) {

    try {
        // 创建虚拟机
        vm::ClawVM vm;

        // 加载模块
        if (!vm.load_module(module)) {
            result.error_message = "Failed to load module into VM: " + vm.last_error;
            return vm::Value::nil();
        }

        if (debug_) {
            std::cout << "[BytecodeExecutor] Loaded module into VM\n";
        }

        // Setup async event loop callback
        vm.runtime.on_future_resolved = [&](std::shared_ptr<vm::FutureValue> future) {
            for (auto& coro : future->waiting_coroutines) {
                if (coro) {
                    vm.runtime.ready_coroutines.push_back(coro);
                }
            }
            future->waiting_coroutines.clear();
        };

        // 执行 main (or initial entry point)
        vm::Value return_value = vm.execute();

        // Event loop: process ready coroutines until all complete
        while (!vm.runtime.ready_coroutines.empty()) {
            auto coro = vm.runtime.ready_coroutines.front();
            vm.runtime.ready_coroutines.pop_front();

            if (!coro || coro->is_complete) continue;

            vm.resume_coroutine(coro);

            // If resuming produced more ready coroutines (e.g. chained awaits),
            // they were added by on_future_resolved in FUTURE_RESOLVE
        }

        // Command dispatch loop: drain command channel and call handlers (P3)
        if (vm.runtime.command_channel) {
            std::vector<vm::Value> commands;
            {
                std::lock_guard<std::mutex> lock(vm.runtime.command_channel->mtx);
                commands.assign(vm.runtime.command_channel->queue.begin(),
                                vm.runtime.command_channel->queue.end());
                vm.runtime.command_channel->queue.clear();
            }
            for (auto& cmd : commands) {
                if (!cmd.is_tuple()) continue;
                auto tup = std::get<std::shared_ptr<vm::TupleValue>>(cmd.data);
                if (tup->elements.size() < 3) continue;
                int64_t req_id = tup->elements[0].as_int();
                std::string name = tup->elements[1].as_string();
                vm::Value args = tup->elements[2];

                auto handler_it = vm.runtime.command_handlers.find(name);
                if (handler_it == vm.runtime.command_handlers.end()) continue;

                for (auto& handler : handler_it->second) {
                    if (!handler.is_closure()) continue;
                    vm::Value result = vm.execute_closure(handler, {args});

                    auto fut_it = vm.runtime.pending_commands.find(req_id);
                    if (fut_it != vm.runtime.pending_commands.end()) {
                        auto future = fut_it->second;
                        future->is_resolved = true;
                        future->resolved_value = result;
                        if (vm.runtime.on_future_resolved) {
                            vm.runtime.on_future_resolved(future);
                        }
                        vm.runtime.pending_commands.erase(fut_it);
                    }
                }
            }
        }

        // Event dispatch loop: drain event channels and call handlers (P1)
        bool dispatched = true;
        int max_rounds = 100;
        while (dispatched && max_rounds-- > 0) {
            dispatched = false;
            for (auto& [name, handlers] : vm.runtime.event_handlers) {
                auto ch_it = vm.runtime.event_channels.find(name);
                if (ch_it == vm.runtime.event_channels.end()) continue;
                auto& ch = ch_it->second;
                std::vector<vm::Value> events;
                {
                    std::lock_guard<std::mutex> lock(ch->mtx);
                    events.assign(ch->queue.begin(), ch->queue.end());
                    ch->queue.clear();
                }
                for (auto& event : events) {
                    dispatched = true;
                    for (auto& handler : handlers) {
                        if (handler.is_closure()) {
                            vm.execute_closure(handler, {event});
                        }
                    }
                }
            }
        }

        // WebTransport bridge dispatch loop (P4): route incoming WT messages
        for (auto& entry : vm.runtime.bridge_registry) {
            if (!entry.connection) continue;
            std::vector<std::string> messages;
            {
                std::lock_guard<std::mutex> lock(entry.connection->queue_mutex);
                messages.assign(entry.connection->incoming_queue.begin(),
                                entry.connection->incoming_queue.end());
                entry.connection->incoming_queue.clear();
            }
            for (auto& msg : messages) {
                if (entry.bridge_kind == "event") {
                    auto& ch = vm.runtime.event_channels[entry.target_name];
                    if (!ch) {
                        ch = vm.runtime.channel_pool.acquire();
                        ch->capacity = 0;
                        ch->closed = false;
                    }
                    std::lock_guard<std::mutex> lock(ch->mtx);
                    ch->queue.push_back(vm::Value::string_v(msg));
                    ch->cv.notify_one();
                } else if (entry.bridge_kind == "command") {
                    if (!vm.runtime.command_channel) {
                        vm.runtime.command_channel = vm.runtime.channel_pool.acquire();
                        vm.runtime.command_channel->capacity = 0;
                        vm.runtime.command_channel->closed = false;
                    }
                    int64_t req_id = vm.runtime.next_command_id++;
                    auto req_tup = vm.runtime.tuple_pool.acquire();
                    req_tup->elements.push_back(vm::Value::int_v(req_id));
                    req_tup->elements.push_back(vm::Value::string_v(entry.target_name));
                    req_tup->elements.push_back(vm::Value::string_v(msg));
                    std::lock_guard<std::mutex> lock(vm.runtime.command_channel->mtx);
                    vm.runtime.command_channel->queue.push_back(vm::Value::tuple_v(req_tup));
                    vm.runtime.command_channel->cv.notify_one();
                } else if (entry.bridge_kind == "stream") {
                    auto& ch = vm.runtime.event_channels[entry.target_name];
                    if (!ch) {
                        ch = vm.runtime.channel_pool.acquire();
                        ch->capacity = 0;
                        ch->closed = false;
                    }
                    std::lock_guard<std::mutex> lock(ch->mtx);
                    ch->queue.push_back(vm::Value::string_v(msg));
                    ch->cv.notify_one();
                }
            }
        }

        // 获取执行统计
        result.stats.vm_instructions_executed = vm.instructions_executed;

        if (debug_) {
            std::cout << "[BytecodeExecutor] Executed " << vm.instructions_executed
                      << " VM instructions\n";
        }

        return return_value;

    } catch (const std::exception& e) {
        result.error_message = std::string("Runtime exception: ") + e.what();
        return vm::Value::nil();
    }
}

} // namespace claw
