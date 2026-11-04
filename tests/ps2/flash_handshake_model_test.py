"""Explore timeout/retry interleavings around the Core-1 flash hooks.

Each transition is one observable protocol step, including the window between
reading the park flag and deciding that the 100 ms deadline has expired. The
old boolean handshake must expose its stale-acknowledgement counterexample;
the ticket handshake must keep XIP disabled only while Core 1 is in its SRAM
park loop for the same request.
"""
from collections import deque
from dataclasses import dataclass, replace
from pathlib import Path
from test_paths import find_project_root

@dataclass(frozen=True)
class State:
    core0: str = 'start_old'
    requested: int = 0
    parked: int = 0
    core1: str = 'load'
    captured: int = 0
    xip_off: bool = False

def explore(use_tickets):
    old, new = 1, 2 if use_tickets else 1
    queue = deque([(State(), ())])
    visited = set()
    completions = 0
    while queue:
        s, path = queue.popleft()
        if s in visited:
            continue
        visited.add(s)
        if s.xip_off and not (
            s.core1 == 'spin' and s.captured == s.requested == s.parked
        ):
            return visited, completions, path
        transitions = []
        def step(action, **changes):
            transitions.append((replace(s, **changes), path + (action,)))

        # Deliberately explore an expired first request followed immediately by
        # a retry. The read of parked and the timeout store are separate steps.
        if s.core0 == 'start_old':
            step('C0 request old', requested=old, core0='read_old_park')
        elif s.core0 == 'read_old_park' and s.parked != old:
            step('C0 reads old request unacknowledged', core0='timeout')
        elif s.core0 == 'timeout':
            step('C0 deadline: cancel old', requested=0, core0='start_new')
        elif s.core0 == 'start_new':
            step('C0 request retry', requested=new, core0='wait_new')
        elif s.core0 == 'wait_new' and s.parked == new:
            step('C0 accepts retry acknowledgement; disables XIP',
                 xip_off=True, core0='finish_flash')
        elif s.core0 == 'finish_flash':
            step('C0 restores QMI/XIP before releasing request',
                 xip_off=False, requested=0, core0='wait_resume')
        elif s.core0 == 'wait_resume' and s.parked != new:
            step('C0 observes retry resume complete', core0='done')
        elif s.core0 == 'done':
            completions += 1

        if s.core1 == 'load' and s.requested:
            step('C1 snapshots request', captured=s.requested, core1='check')
        elif s.core1 == 'check':
            if s.requested == s.captured:
                step('C1 enters pause hook with IRQs disabled', core1='pause_hook')
            else:
                step('C1 rejects canceled snapshot', core1='load', captured=0)
        elif s.core1 == 'pause_hook':
            step('C1 pause hook completes', core1='after_pause')
        elif s.core1 == 'after_pause':
            if s.requested == s.captured:
                step('C1 checks ticket before acknowledgement', core1='ack')
            else:
                step('C1 skips park for canceled pause', core1='resume_hook')
        elif s.core1 == 'ack':
            step('C1 publishes acknowledgement', parked=s.captured, core1='spin')
        elif s.core1 == 'spin' and s.requested != s.captured:
            step('C1 observes release and enters XIP resume hook', core1='resume_hook')
        elif s.core1 == 'resume_hook':
            step('C1 resume hook finishes', core1='clear_ack')
        elif s.core1 == 'clear_ack':
            step('C1 clears acknowledgement after resume',
                 parked=0, captured=0, core1='load')

        queue.extend(transitions)
    return visited, completions, None

old_states, _, counterexample = explore(False)
assert counterexample is not None, 'Model failed to expose the original race'
ticket_states, completions, counterexample_ticket = explore(True)
assert counterexample_ticket is None, counterexample_ticket
assert completions > 0, 'Ticket protocol never completed a retry'

def next_ticket(previous):
    value = (previous + 1) & 0xffffffff
    return value if value else 1
assert next_ticket(0) == 1 and next_ticket(0xfffffffe) == 0xffffffff
assert next_ticket(0xffffffff) == 1

source = (find_project_root() / 'vendor-libraries' / 'FatFS' / 'src' /
          'FatFSFlash.cpp').read_text()
assert 'if (++next_request_ticket == 0u) ++next_request_ticket;' in source
assert '__atomic_store_n(&parked, ticket, __ATOMIC_RELEASE)' in source
assert 'while (__atomic_load_n(&parked, __ATOMIC_ACQUIRE) != ticket)' in source
assert 'while (__atomic_load_n(&requested, __ATOMIC_ACQUIRE) == ticket)' in source

print(f'Flash handshake model: old boolean race reproduced; all {len(ticket_states)} '
      f'ticket states safe, retry completion reachable, nonzero wrap passed')
