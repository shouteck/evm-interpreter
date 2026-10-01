#include "evm/evm.hpp"
#include "evm/opcode.hpp"
#include "evm/keccak.hpp"

#include <stdexcept>
#include <utility>
#include <limits>

namespace evm {

Evm::Evm(Bytes code, Host& host, CallContext call, Gas gas)
: code_(std::move(code)), host_(host), call_(std::move(call)), gas_(gas),
  stack_(MAX_STACK)
    {
    // walk the wall once: mark real JUMPDEST positions, skip PUSH data
    jumpdests_.assign(code_.size(), false);
    for (std::size_t i = 0; i < code_.size();)
    {
        if (code_[i] == op::JUMPDEST) jumpdests_[i] = true;
        i += 1 + (is_push(code_[i]) ? push_size(code_[i]) : 0);
    }
}

ExecResult Evm::run(std::size_t step_cap) {
    std::size_t cp = host_.checkpoint();   // frame-entry mark: revertable window
    const Gas initial = gas_;
    std::size_t steps = 0;
    while (!halted_ && steps < step_cap) {
        std::size_t opc = pc_;                               // instr about to run
        Byte      o   = opc < code_.size() ? code_[opc] : (Byte)op::STOP;
        step();
        if (on_step) on_step(*this, opc, o);                 // tracers see post-state
        ++steps;
    }
    if (!halted_) return result_;          // step-capped: caller decides what that means
    if (result_.reason == StopReason::Revert) {
        host_.revert(cp);                  // soft undo — gas spent, writes undone
    } else if (result_.reason == StopReason::Halt && result_.error != Error::None) {
        host_.revert(cp);                  // exceptional: everything undone,
        gas_ = 0;                          // all gas burned, output discarded
        result_.gas_left = 0;
        result_.output.clear();
    }
    // settle the refund: min(refund, spent/2) — not on exceptional halts
    if (refund_ > 0 && !(result_.reason == StopReason::Halt && result_.error != Error::None)) {
        Gas r = std::min<Gas>(refund_, (initial - gas_) / 2);
        gas_ += r;
        result_.gas_left = gas_;
    }
    return result_;
}

// the price list — one lookup
static Gas gas_cost(Byte op) {
    switch (op) {
        case op::STOP: case op::RETURN:
        case op::REVERT:                             return 0;   // halting is free
        case op::JUMPDEST:                           return 1;
        case op::PC: case op::MSIZE: case op::GAS:
        case op::ADDRESS: case op::ORIGIN: case op::CALLER:
        case op::CALLVALUE: case op::CALLDATASIZE: case op::CODESIZE:
        case op::GASPRICE: case op::RETURNDATASIZE: case op::POP:
        case op::COINBASE: case op::TIMESTAMP: case op::NUMBER:
        case op::PREVRANDAO: case op::GASLIMIT: case op::CHAINID:
        case op::SELFBALANCE:                        return 2;   // "base" tier
        case op::BLOCKHASH:                          return 20;
        case op::ADD: case op::SUB: case op::NOT:
        case op::LT: case op::GT: /* ... */          return 3;   // "verylow"
        case op::MUL: case op::DIV: case op::SDIV:
        case op::MOD: case op::SMOD:
        case op::SIGNEXTEND:                         return 5;   // "low"
        case op::ADDMOD: case op::MULMOD:
        case op::JUMP:                               return 8;   // "mid"
        case op::JUMPI:                              return 10;
        case op::MLOAD: case op::MSTORE:
        case op::MSTORE8:                            return 3;
        case op::EXP:                                return 10;  // + 10/exponent byte (Constantinople)
        case op::SHA3:                               return 30;  // + 6/word
        case op::LOG0: case op::LOG1: case op::LOG2:
        case op::LOG3: case op::LOG4:                return 0;   // metered in-arm: 375 + 375/topic + 8/byte
        case op::SLOAD:                              return 50;  // Frontier schedule
        case op::BALANCE: case op::EXTCODEHASH:      return 20;
        case op::EXTCODESIZE: case op::EXTCODECOPY:  return 20;
        case op::CALL: case op::DELEGATECALL:
        case op::STATICCALL:                         return 40;  // + 9000 value + 25000 new acct
        case op::CREATE: case op::CREATE2:           return 32000;
        case op::SELFDESTRUCT:                       return 0;   // Frontier: suicide is free
        case op::SSTORE:                             return 0;   // metered in-arm: 20000/5000 + refunds
        default:                                     return 3;   // PUSH/DUP/SWAP tier
    }
}

// finger-moving procedure for the clerk
// there are 2 checks
// check 1: is dest even on the wall (the wall is fixed at construction)
// check 2: is there a bookmark there
void Evm::jump(const U256& dest) {
    if (dest >= U256(code_.size()) || !jumpdests_[(std::size_t)dest.low64()]) {
        halt(StopReason::Halt, Error::InvalidJump);
        return;
    }
    // the clerk picking its finger up and putting it on a different line on the wall
    pc_ = (std::size_t)dest.low64();    
}

// does the desk cover from off + 0 to off + len - 1?
void Evm::mem_expand(const U256& off, std::size_t len) {
    if (len == 0) return;              // an empty range touches no paper
    if ((off.l[1] | off.l[2] | off.l[3]) != 0) {
        halt(StopReason::Halt, Error::OutOfBounds);
        return;
    }
    std::uint64_t o = off.low64();
    // checking if off + len - 1 is still within the desk limits
    if (o > std::numeric_limits<std::size_t>::max() - len) {
        halt(StopReason::Halt, Error::OutOfBounds);
        return;
    }
    // rounding number of papers up if there's a remainder
    std::size_t need = ((static_cast<std::size_t>(o) + len + 31) / 32) * 32; 

    // if we need more paper than we have, expand the desk
    if (need > memory_.size()) {
        std::size_t old_w = memory_.size() / 32;
        std::size_t new_w = need / 32;
        // 3n + n^2/512
        charge_gas(3*(Gas)(new_w - old_w)
                 + (Gas)(new_w*new_w)/512 - (Gas)(old_w*old_w)/512);
        if (halted_) return;
        memory_.resize(need);
    }
}

// CREATE's address: last 20 bytes of keccak(rlp([sender, nonce]))
static Address create_address(const Address& sender, std::uint64_t nonce) {
    Bytes p; p.push_back(0x94);                       // RLP: 20-byte string
    p.insert(p.end(), sender.begin(), sender.end());
    if (nonce == 0)         p.push_back(0x80);        // empty string
    else if (nonce < 0x80)  p.push_back((Byte)nonce); // single byte < 128
    else {
        Bytes nb;
        for (std::uint64_t n = nonce; n; n >>= 8) nb.insert(nb.begin(), (Byte)(n & 0xff));
        p.push_back((Byte)(0x80 + nb.size()));
        p.insert(p.end(), nb.begin(), nb.end());
    }
    Bytes enc; enc.push_back((Byte)(0xc0 + p.size())); // RLP short list
    enc.insert(enc.end(), p.begin(), p.end());
    Byte h[32]; keccak256(enc.data(), enc.size(), h);
    Address a; std::copy(h + 12, h + 32, a.begin());
    return a;
}

// CREATE2's address: last 20 of keccak(0xff ++ sender ++ salt ++ keccak(init))
static Address create2_address(const Address& sender, const U256& salt, const Bytes& init) {
    Byte ih[32]; keccak256(init.data(), init.size(), ih);
    Byte sbuf[32]; to_bytes32(salt, sbuf);
    Bytes m; m.reserve(85);
    m.push_back(0xff);
    m.insert(m.end(), sender.begin(), sender.end());
    m.insert(m.end(), sbuf, sbuf + 32);
    m.insert(m.end(), ih, ih + 32);
    Byte h[32]; keccak256(m.data(), m.size(), h);
    Address a; std::copy(h + 12, h + 32, a.begin());
    return a;
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
    Gas cost = gas_cost(op_code);
    if ((gas_ -= cost) < 0) {
        halt(StopReason::Halt, Error::OutOfGas);
        return false;
    }

    switch (op_code) {
        case op::STOP:
            halt(StopReason::Stop);
            return false;
        case op::POP:
            pop();
            break;
        // the call family — same machinery, different envelope plumbing:
        // CALL:        child IS the callee   (address=callee, caller=us, value=operand)
        // DELEGATECALL: callee's WALL in OUR office (address stays us, caller/value pass through)
        // STATICCALL:  CALL + read-only flag — writes halt the frame
        case op::CALL: case op::DELEGATECALL: case op::STATICCALL: {
            const bool delegate = (op_code == op::DELEGATECALL);
            const bool staticc  = (op_code == op::STATICCALL);
            U256 gas = pop(), to = pop();
            U256 value;                                        // delegate/static carry no cash operand
            if (!delegate && !staticc) value = pop();
            U256 in_offset = pop(), in_len = pop(), out_offset = pop(), out_len = pop();

            if ((in_len.l[1]|in_len.l[2]|in_len.l[3]) != 0 ||
                (out_len.l[1]|out_len.l[2]|out_len.l[3]) != 0) {
                halt(StopReason::Halt, Error::OutOfBounds); return false;
            }

            mem_expand(in_offset,  (std::size_t)in_len.low64());
            mem_expand(out_offset, (std::size_t)out_len.low64());
            if (halted_) return false;

            Byte tbuf[32];
            to_bytes32(to, tbuf);

            Address callee;
            // copy the last 20 bytes of tbuf into callee from paper format
            std::copy(tbuf+12, tbuf+32, callee.begin());

            // read-only propagates: a static parent's children are static too
            bool child_static = call_.is_static || staticc;

            // cash inside a read-only frame is a hard fault, not a refusal
            if (child_static && value != U256()) {
                halt(StopReason::Halt, Error::StaticViolation); return false;
            }

            // 1. too many nested calls
            // 2. not enough cash to pay the business
            if (call_.depth >= 1024 || (value != U256() && host_.balance(call_.address) < value)) {
                push(U256()); break;
            }

            // carrying cash costs extra; seeding a fresh business costs more
            if (value != U256()) {
                charge_gas(9000 + (host_.exists(callee) ? 0 : 25000));
                if (halted_) return false;
            }

            // copying the input data from the desk to the child's letter
            Bytes in(memory_.begin() + (std::size_t)in_offset.low64(), memory_.begin() + (std::size_t)(in_offset + in_len).low64());

            CallContext child = delegate
                ? CallContext{ call_.address, call_.caller, call_.origin,
                               call_.call_value, std::move(in), call_.depth + 1, child_static }
                : CallContext{ callee, call_.address, call_.origin,
                               value, std::move(in), call_.depth + 1, child_static };

            // bookmark the receipt's stack current height before the child touches the world
            std::size_t cp = host_.checkpoint();

            // the cash move from caller to callee (delegatecall never moves cash)
            if (value != U256()) host_.transfer(call_.address, callee, value);

            // a call may forward at most 63/64 of parent's remaining gas
            Gas fwd = std::min<Gas>((Gas)gas.low64(), gas_ - gas_/64);
            gas_ -= fwd;
            Evm kid(Bytes(host_.code(callee)), host_, std::move(child), fwd);
            kid.on_step = on_step;                           // tracer follows the child
            ExecResult r = kid.run();

            // unused stamps come home
            gas_ += r.gas_left;

            bool ok = r.reason == StopReason::Return || r.reason == StopReason::Stop;
            if (!ok) {
                // undo the receipts
                host_.revert(cp);
            }

            returndata_ = r.output;

            // copy report onto the desk, clipped to out_len
            std::size_t n = std::min<std::size_t>(r.output.size(), (std::size_t)out_len.low64());
            std::copy_n(r.output.begin(), n, memory_.begin() + (std::size_t)out_offset.low64());
            push(U256(ok ? 1 : 0));
            break;
        }
        // founding a new business: the desk slice is INIT code — the child
        // runs it, and whatever it RETURNs gets hung on the new wall.
        // CREATE:  addr = keccak(rlp(sender, nonce))[12:]
        // CREATE2: addr = keccak(0xff ++ sender ++ salt ++ keccak(init))[12:]
        case op::CREATE: case op::CREATE2: {
            if (call_.is_static) {   // can't open a business in read-only mode
                halt(StopReason::Halt, Error::StaticViolation); return false;
            }
            U256 value = pop(), off = pop(), len = pop();
            U256 salt;
            if (op_code == op::CREATE2) salt = pop();
            if ((len.l[1]|len.l[2]|len.l[3]) != 0) {
                halt(StopReason::Halt, Error::OutOfBounds); return false;
            }
            if (op_code == op::CREATE2)
                charge_gas(6 * (Gas)((len.low64() + 31) / 32));   // hashing the init
            mem_expand(off, (std::size_t)len.low64());
            if (halted_) return false;

            if (call_.depth >= 1024 || (value != U256() && host_.balance(call_.address) < value)) {
                push(U256()); break;                              // refusal, not a fault
            }

            Bytes init(memory_.begin() + (std::size_t)off.low64(),
                       memory_.begin() + (std::size_t)(off + len).low64());
            Address na = op_code == op::CREATE2
                ? create2_address(call_.address, salt, init)
                : create_address(call_.address, host_.nonce(call_.address));
            host_.bump_nonce(call_.address);          // burns the nonce even if init fails

            std::size_t cp = host_.checkpoint();
            host_.create_account(na);
            if (value != U256()) host_.transfer(call_.address, na, value);

            CallContext child{ na, call_.address, call_.origin, value,
                               Bytes{}, call_.depth + 1, call_.is_static };
            Gas fwd = gas_ - gas_/64;
            gas_ -= fwd;
            Evm kid(std::move(init), host_, std::move(child), fwd);
            kid.on_step = on_step;                           // tracer follows the child
            ExecResult r = kid.run();
            gas_ += r.gas_left;
            returndata_ = r.output;

            bool ok = r.reason == StopReason::Return || r.reason == StopReason::Stop;
            if (ok) {
                host_.install_code(na, r.output);     // the return value IS the wall
                push(from_bytes(na.data(), na.size()));
            } else {
                host_.revert(cp);
                push(U256());                         // failed birth -> zero address
            }
            break;
        }
        case op::SHA3: {
            U256 off = pop(), len = pop();
            if ((len.l[1] | len.l[2] | len.l[3]) != 0) {
                halt(StopReason::Halt, Error::OutOfBounds);
                return false;
            }
            charge_gas(6 * (Gas)((len.low64() + 31) / 32)); // 6 per page pressed
            mem_expand(off, (std::size_t)len.low64());
            if (halted_) return false;         
            push(keccak256(&memory_[(std::size_t)off.low64()], (std::size_t)len.low64()));
            break;   
        }
        case op::TIMESTAMP:   push(U256(host_.block().timestamp));  break;
        case op::NUMBER:      push(U256(host_.block().number));     break;
        case op::COINBASE:    push(host_.block().coinbase);         break;
        case op::PREVRANDAO:  push(host_.block().difficulty);       break;
        case op::GASLIMIT:    push(host_.block().gas_limit);        break;
        case op::CHAINID:     push(host_.block().chain_id);         break;
        case op::BASEFEE:     push(host_.block().base_fee);         break;
        case op::GASPRICE:    push(host_.block().gas_price);        break;
        case op::SELFBALANCE: push(host_.balance(call_.address));   break;            
        // clerk phones the country to find out about any business's balance
        case op::BALANCE: {
            U256 a = pop();
            Byte buf[32] = {};
            to_bytes32(a, buf);
            Address addr;
            std::copy(buf + 12, buf + 32, addr.begin());
            push(host_.balance(addr));
            break;
        }
        // looking over the fence: another business's wall size/hash/copy
        case op::EXTCODESIZE: {
            U256 a = pop();
            Byte buf[32] = {}; to_bytes32(a, buf);
            Address addr; std::copy(buf + 12, buf + 32, addr.begin());
            push(U256(host_.code(addr).size()));
            break;
        }
        case op::EXTCODEHASH: {
            U256 a = pop();
            Byte buf[32] = {}; to_bytes32(a, buf);
            Address addr; std::copy(buf + 12, buf + 32, addr.begin());
            if (!host_.exists(addr)) { push(U256()); break; }       // no such business
            Byte h[32]; keccak256(host_.code(addr).data(), host_.code(addr).size(), h);
            push(from_bytes(h, 32));                               // empty wall -> keccak("")
            break;
        }
        case op::EXTCODECOPY: {
            U256 a = pop(), dest = pop(), off = pop(), len = pop();
            if ((len.l[1] | len.l[2] | len.l[3]) != 0) {
                halt(StopReason::Halt, Error::OutOfBounds); return false;
            }
            charge_gas(3 * (Gas)((len.low64() + 31) / 32));
            mem_expand(dest, (std::size_t)len.low64());
            if (halted_) return false;
            Byte buf[32] = {}; to_bytes32(a, buf);
            Address addr; std::copy(buf + 12, buf + 32, addr.begin());
            const Bytes& c = host_.code(addr);
            std::size_t n = (std::size_t)len.low64(), copy = 0;
            if (off < U256(c.size()))
                copy = std::min<std::size_t>(n, c.size() - (std::size_t)off.low64());
            std::size_t d = (std::size_t)dest.low64();
            for (std::size_t j = 0; j < copy; ++j) memory_[d + j] = c[(std::size_t)off.low64() + j];
            for (std::size_t j = copy; j < n; ++j)  memory_[d + j] = 0;
            break;
        }
        // the gazette's archive: hash of a recent block (0 outside the window)
        case op::BLOCKHASH: {
            push(host_.blockhash(pop().low64()));
            break;
        }
        // the last sub-call's report, as its own buffer
        case op::RETURNDATASIZE: {
            push(U256(returndata_.size()));
            break;
        }
        case op::RETURNDATACOPY: {
            U256 dest = pop(), off = pop(), len = pop();
            if ((len.l[1] | len.l[2] | len.l[3]) != 0) {
                halt(StopReason::Halt, Error::OutOfBounds); return false;
            }
            // reading past the report's end aborts the frame
            if (off > U256(returndata_.size()) ||
                U256(returndata_.size()) - off < len) {
                halt(StopReason::Halt, Error::OutOfBounds); return false;
            }
            charge_gas(3 * (Gas)((len.low64() + 31) / 32));
            mem_expand(dest, (std::size_t)len.low64());
            if (halted_) return false;
            std::copy_n(returndata_.begin() + (std::size_t)off.low64(),
                        (std::size_t)len.low64(),
                        memory_.begin() + (std::size_t)dest.low64());
            break;
        }
        case op::CODECOPY: {
            U256 dest = pop(), off = pop(), len = pop();
            if ((len.l[1] | len.l[2] | len.l[3]) != 0) {
                halt(StopReason::Halt, Error::OutOfBounds);
                return false;
            }
            charge_gas(3 * (Gas)((len.low64() + 31) / 32)); // 3 per page copied
            mem_expand(dest, (std::size_t)len.low64());
            if (halted_) return false;

            std::size_t d = (std::size_t)dest.low64();
            std::size_t n = (std::size_t)len.low64();
            std::size_t copy = 0;
            if (off < U256(code_.size()))
                copy = std::min<std::size_t>(n, code_.size() - (std::size_t)off.low64());
            for (std::size_t j = 0; j < copy; ++j)
                memory_[d + j] = code_[(std::size_t)off.low64() + j];
            for (std::size_t j = copy; j < n; ++j)
                memory_[d + j] = 0;
            break;            
        }
        case op::CODESIZE: {
            push(U256(code_.size()));
            break;
        }
        // cash clipped to the envelope which are payment for the business
        case op::CALLVALUE: {
            push(call_.call_value);
            break;
        }
        // the resident who mailed the very first envelope
        case op::ORIGIN: {
            push(from_bytes(call_.origin.data(), call_.origin.size()));
            break;
        }            
        // clerk reading the resident's address from the envelope and dropping it on top of the pile
        case op::CALLER: {
            push(from_bytes(call_.caller.data(), call_.caller.size()));
            break;
        }
        // clerk reading the address of the business from the envelope and dropping it on top of the pile
        case op::ADDRESS: {
            push(from_bytes(call_.address.data(), call_.address.size()));
            break;
        }
        // clerk copying from the letter to the desk, from position off to off + len - 1 to desk position dest to dest + len - 1
        case op::CALLDATACOPY: {
            // dest square, letter start, count
            U256 dest = pop(), off = pop(), len = pop();
            // desk only can accommodate 64 bits
            if ((len.l[1] | len.l[2] | len.l[3]) != 0) {
                halt(StopReason::Halt, Error::OutOfBounds);
                return false;
            }
            charge_gas(3 * (Gas)((len.low64() + 31) / 32)); // 3 per page copied
            mem_expand(dest, (std::size_t)len.low64());
            if (halted_) return false;

            std::size_t d = (std::size_t)dest.low64();
            std::size_t n = (std::size_t)len.low64();
            std::size_t copy = 0;
            if (off < U256(call_.calldata.size()))
                copy = std::min<std::size_t>(n, call_.calldata.size() - (std::size_t)off.low64());
            for (std::size_t j = 0; j < copy; ++j)
                memory_[d + j] = call_.calldata[(std::size_t)off.low64() + j];
            for (std::size_t j = copy; j < n; ++j)
                memory_[d + j] = 0;
            break;
        }
        // clerk reading the letter from position i to i + 31 and drops it ontop of the pile
        case op::CALLDATALOAD: {
            U256 i = pop();
            Byte buf[32] = {};
            // is the position i within the letter?
            if (i < U256(call_.calldata.size())) {
                std::size_t off = (std::size_t)i.low64();
                std::size_t n = call_.calldata.size() - off;
                if (n > 32) n = 32;
                for (std::size_t j = 0; j < n; ++j) {
                    buf[j] = call_.calldata[off + j];
                }
            }
            push(from_bytes(buf, 32));
            break;
        }
        case op::CALLDATASIZE: {
            push(U256(call_.calldata.size()));
            break;
        }
        // clerk filing the report with a big red "VOID" stamp on it
        case op::REVERT: {
            U256 off = pop(), len = pop();
            mem_expand(off, (std::size_t)len.low64());
            if (halted_) return false;
            result_.output.assign(memory_.begin() + off.low64(),
                memory_.begin() + off.low64() + len.low64());
            halt(StopReason::Revert);
            return false;
        }
        // closing up shop: sweep the cash to the beneficiary, done for the day
        case op::SELFDESTRUCT: {
            if (call_.is_static) {
                halt(StopReason::Halt, Error::StaticViolation); return false;
            }
            U256 b = pop();
            Byte buf[32] = {}; to_bytes32(b, buf);
            Address ben; std::copy(buf + 12, buf + 32, ben.begin());
            host_.kill(call_.address, ben);
            halt(StopReason::Stop);
            return false;
        }
        // conditional jump
        case op::JUMPI: {
            U256 dest = pop(), cond = pop();
            if (!cond.is_zero()) {
                jump(dest);
                if (halted_) return false;
            }
            break;
        }
        case op::PC: {
            push(U256(pc_ - 1));
            break;
        }
        case op::GAS: {
            push(U256(gas_));
            break;
        }
        case op::JUMP: {
            jump(pop());
            if (halted_) return false;
            break;
        }
        case op::JUMPDEST: 
            break;
        case op::SSTORE: {
            if (call_.is_static) {   // read-only frame: cabinet is off-limits
                halt(StopReason::Halt, Error::StaticViolation); return false;
            }
            U256 key = pop(), v = pop();
            U256 old = host_.sload(call_.address, key);
            charge_gas((old == U256() && v != U256()) ? 20000 : 5000); // fresh vs dirty slot
            if (halted_) return false;
            if (v == U256() && old != U256()) refund_ += 15000;        // freeing a slot pays back
            host_.sstore(call_.address, key, v);
            break;
        }
        // clerk phones the country to read a filing-cabinet slot
        case op::SLOAD: {
            // key - which slot
            // call_.address - which filing cabinet (each business has its own, you can't read another business's cabinet)
            U256 key = pop();
            push(host_.sload(call_.address, key));
            break;
        }
        case op::RETURN: {
            U256 off = pop(), len = pop();
            // clerk may need more than one paper
            mem_expand(off, (std::size_t)len.low64());
            if (halted_) return false;
            // clerk copies the papers from the desk to the output slip
            result_.output.assign(memory_.begin() + off.low64(),
                memory_.begin() + off.low64() + len.low64());
            halt(StopReason::Return);
            return false;            
        }
        // clerk reporting how big its desk currently is
        case op::MSIZE: {
            push(U256(memory_.size()));
            break;
        }
        // MSTORE version where clerk writes just one square of the desk
        case op::MSTORE8: {
            U256 off = pop(), v = pop();
            mem_expand(off, 1);
            if (halted_) return false;
            memory_[(std::size_t)off.low64()] = (Byte)(v.low64() & 0xff);
            break;
        }            
        case op::MSTORE: {
            // off - where on desk
            // v - what to write
            U256 off = pop(), v = pop();
            mem_expand(off, 32);
            if (halted_) return false;
            to_bytes32(v, &memory_[(std::size_t)off.low64()]);
            break;
        }
        // grab the offset slip from the pile
        // clerk takes 1 paper off the desk, desk spot off + 0 to off + len - 1
        // transcribes it onto a slip (desk stores words in paper order)
        // drops it on top of the pile
        case op::MLOAD: {
            U256 off = pop();
            mem_expand(off, 32);
            if (halted_) return false;
            push(from_bytes(&memory_[(std::size_t)off.low64()], 32));
            break;
        }
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
            unsigned eb = 0;
            for (int b = 0; b < 32; ++b) {
                if (byte_at(e, b)) {
                    eb = 32 - b;
                    break;
                }
            }
            charge_gas(10 * (Gas)eb); // 10 per exponent byte (Constantinople)
            if (halted_) return false;
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
            // clerk copies a desk slice (the announcement text) plus 0-4 topics slip
            // posts it to the country's permanent event log
            // stamped with this business's address
            if (is_log(op_code)) {
                if (call_.is_static) {   // read-only frame: no posting
                    halt(StopReason::Halt, Error::StaticViolation); return false;
                }
                unsigned n = op_code - op::LOG0; // how many topics (0..4)
                U256 off = pop(), len = pop();
                if ((len.l[1] | len.l[2] | len.l[3]) != 0) {
                    halt(StopReason::Halt, Error::OutOfBounds);
                    return false;
                }
                charge_gas(375 + 375*(Gas)n + 8*(Gas)len.low64());
                mem_expand(off, (std::size_t)len.low64());
                if (halted_) return false;

                Bytes data(memory_.begin() + (std::size_t)off.low64(), 
                    memory_.begin() + (std::size_t)off.low64() + (std::size_t)len.low64());
                
                std::vector<U256> topics(n);
                for (unsigned i = 0; i < n; ++i) topics[i] = pop(); // labels, off the pile

                host_.log(call_.address, std::move(data), std::move(topics));
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

// metered surcharge — same OOG rule as the flat fare
void Evm::charge_gas(Gas g) {
    gas_ -= g;
    if (gas_ < 0) halt(StopReason::Halt, Error::OutOfGas);
}

} // namespace evm
