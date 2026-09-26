#include "evm/evm.hpp"
#include "evm/opcode.hpp"

#include <stdexcept>
#include <utility>

namespace evm {

Evm::Evm(Bytes code, Host& host, CallContext call, Gas gas)
    : code_(std::move(code)), host_(host), call_(std::move(call)), gas_(gas) {}

ExecResult Evm::run() {
    while (step()) {}
    return result_;
}

static U256 pow_u256(U256 base, U256 exp) {
    U256 r(1);
    for (int i = 0; i < 256; ++i) {
        if (bit(exp, i)) r = r * base;
        base = base * base;
    }
    return r;
}

bool Evm::step() {
    if (halted_) return false;
    if (pc_ >= code_.size()) {
        halt(StopReason::Stop);
        return false;
    }

    Byte op_code = code_[pc_++];

    switch (op_code) {
        case op::STOP:
            halt(StopReason::Stop);
            return false;
        case op::POP:
            pop();
            break;
        case op::SAR: {
            U256 s = pop(), v = pop();
            if (s >= U256(256)) {
                push(bit(v, 255) ? U256::max() : U256());
            } else {
                unsigned sh = (unsigned)s.low64();
                U256 r = v >> sh;
                if (bit(v, 255)) {
                    U256 mask = U256::max() << (256 - sh);
                    r = r | mask;
                }
                push(r);
            }
            break;
        }
        case op::SIGNEXTEND: {
            U256 k = pop(), x = pop();
            if (k >= U256(32)) {
                push(x);
                break;
            }
            unsigned sbit = (unsigned)k.low64() * 8 + 7;
            if (bit(x, sbit))
                x = x | (U256::max() << (sbit + 1));
            else
                x = x & ~(U256::max() << (sbit + 1));
            push(x);
            break;
        }
        case op::EXP: {
            U256 base = pop();
            U256 e = pop();
            push(pow_u256(base, e));
            break;
        }
        case op::ADD: {
            U256 a = pop(); U256 b = pop();
            push(a + b);
            break;
        }
        case op::SUB: {
            U256 a = pop(); U256 b = pop();
            push(a - b);
            break;
        }
        case op::DIV: {
            U256 a = pop(); U256 b = pop();
            push(a / b);
            break;
        }
        case op::MUL: {
            U256 a = pop(); U256 b = pop();
            push(a * b);
            break;
        }
        case op::SDIV: {
            U256 a = pop(); U256 b = pop();
            push(sdiv(a, b));
            break;
        }
        case op::MOD: {
            U256 a = pop(); U256 b = pop();
            push(a % b);
            break;
        }
        case op::SMOD: {
            U256 a = pop(); U256 b = pop();
            push(smod(a, b));
            break;
        }
        case op::LT: {
            U256 a = pop(); U256 b = pop();
            push(a < b ? U256(1) : U256(0));
            break;
        }
        case op::GT: {
            U256 a = pop(); U256 b = pop();
            push(a > b ? U256(1) : U256(0));
            break;
        }
        case op::SLT: {
            U256 a = pop(); U256 b = pop();
            push(slt(a, b));
            break;
        }
        case op::SGT: {
            U256 a = pop(); U256 b = pop();
            push(sgt(a, b));
            break;
        }
        case op::EQ: {
            U256 a = pop(); U256 b = pop();
            push(a == b ? U256(1) : U256(0));
            break;
        }
        case op::AND: {
            U256 a = pop(); U256 b = pop();
            push(a & b);
            break;
        }
        case op::OR: {
            U256 a = pop(); U256 b = pop();
            push(a | b);
            break;
        }
        case op::XOR: {
            U256 a = pop(); U256 b = pop();
            push(a ^ b);
            break;
        }
        case op::ISZERO: {
            push(pop().is_zero() ? U256(1) : U256(0));
            break;
        }
        case op::NOT: {
            push(~pop());
            break;
        }
        case op::ADDMOD: {
            U256 a = pop(); U256 b = pop(); U256 n = pop();
            push(addmod(a, b, n));
            break;
        }
        case op::MULMOD: {
            U256 a = pop(); U256 b = pop(); U256 n = pop();
            push(mulmod(a, b, n));
            break;
        }
        case op::BYTE: {
            U256 i = pop(), x = pop();
            push(i < U256(32) ? U256(byte_at(x, (unsigned)i.low64())) : U256());
            break;
        }
        case op::SHL: {
            U256 s = pop(), v = pop();
            push(s < U256(256) ? v << (unsigned)s.low64() : U256());
            break;
        }
        case op::SHR: {
            U256 s = pop(), v = pop();
            push(s < U256(256) ? v >> (unsigned)s.low64() : U256());
            break;
        }
        default:
            if (is_push(op_code)) {
                unsigned n = push_size(op_code);
                Byte buf[32] = {};
                for (unsigned i = 0; i < n && pc_ + i < code_.size(); ++i) {
                    buf[i] = code_[pc_ + i];
                }
                push(from_bytes(buf, n));
                pc_ += n;
                break;
            }
            if (is_dup(op_code)) {
                unsigned n = op_code - 0x7f;
                if (sp_ < n) { 
                    halt(StopReason::Halt, Error::StackUnderflow); 
                    return false; 
                }
                push(stack_[sp_ - n]);
                break;
            }
            if (is_swap(op_code)) {
                unsigned n = op_code - 0x8f;
                if (sp_ < n + 1) { 
                    halt(StopReason::Halt, Error::StackUnderflow); 
                    return false; 
                }
                std::swap(stack_[sp_ - 1], stack_[sp_ - 1 - n]);
                break;
            }
            halt(StopReason::Halt, Error::InvalidOpcode);
            return false;
    }

    return !halted_;    // a helper may have halted mid-case (e.g. pop underflow)
}

void Evm::push(const U256& v) {
    if (sp_ >= MAX_STACK) {
        halt(StopReason::Halt, Error::StackOverflow);
        return;
    }
    stack_[sp_++] = v;
}

U256 Evm::pop() {
    if (sp_ == 0) {
        halt(StopReason::Halt, Error::StackUnderflow);
        return U256();
    }
    return stack_[--sp_];  
}

void Evm::halt(StopReason r, Error e) {
    halted_         = true;
    result_.reason  = r;
    result_.error   = e;
    result_.gas_left = gas_;
    // result_.output filled by RETURN/REVERT cases, not here
}

} // namespace evm
