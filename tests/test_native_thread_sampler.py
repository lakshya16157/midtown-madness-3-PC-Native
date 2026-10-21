"""A failed diagnostic read must never leave an observer-owned suspend."""
import ctypes as C
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/diagnostics'))
from sample_threads import sample_context, open_target_thread, evict_terminated_threads
sys.path.pop(0)


class SamplingCleanup(unittest.TestCase):
    def exercise(self, count=0, valid=True, raises=False, resume_value=1):
        storage=C.create_string_buffer(1232)
        struct.pack_into('<Q',storage,248,0x12345678)
        calls=[]
        def suspend(handle): calls.append('suspend');return count
        def resume(handle): calls.append('resume');return resume_value
        def context(handle,address):
            calls.append('context')
            if raises:raise RuntimeError('injected context failure')
            return valid
        clock=iter([5.0,5.02]).__next__
        self.calls=calls
        return sample_context(17,C.addressof(storage),suspend,resume,context,clock)

    def test_success_is_resumed_before_record(self):
        record=self.exercise()
        self.assertEqual(record[1],0x12345678)
        self.assertAlmostEqual(record[2],20)
        self.assertTrue(record[3])
        self.assertEqual(self.calls,['suspend','context','resume'])

    def test_return_slot_is_read_at_rsp_while_suspended(self):
        storage=C.create_string_buffer(1232)
        struct.pack_into('<Q',storage,248,0x12345678)
        struct.pack_into('<Q',storage,152,0x7000)
        calls=[]
        def read_slot(rsp): calls.append(('slot',rsp));return 0xABCDEF
        record=sample_context(17,C.addressof(storage),lambda h:calls.append('suspend') or 0,
                              lambda h:calls.append('resume') or 1,lambda h,a:calls.append('context') or True,
                              iter([1.0,1.01]).__next__,read_slot)
        self.assertEqual(record[5],0xABCDEF)
        self.assertEqual(calls,['suspend','context',('slot',0x7000),'resume'])

    def test_failed_context_still_resumes(self):
        record=self.exercise(valid=False)
        self.assertFalse(record[3])
        self.assertEqual(record[1],0)
        self.assertEqual(self.calls,['suspend','context','resume'])

    def test_exception_still_resumes(self):
        with self.assertRaisesRegex(RuntimeError,'injected'):
            self.exercise(raises=True)
        self.assertEqual(self.calls,['suspend','context','resume'])

    def test_failed_suspend_does_not_resume_someone_else(self):
        self.assertIsNone(self.exercise(count=0xFFFFFFFF))
        self.assertEqual(self.calls,['suspend'])

    def test_existing_suspend_is_preserved(self):
        record=self.exercise(count=2,resume_value=3)
        self.assertEqual(record[4],2)
        self.assertFalse(record[3])
        self.assertEqual(self.calls,['suspend','resume'])

    def test_failed_resume_is_never_reported_as_success(self):
        with self.assertRaisesRegex(OSError,'ResumeThread'):
            self.exercise(resume_value=0xFFFFFFFF)
        self.assertEqual(self.calls,['suspend','context','resume'])


class ThreadHandleOwnership(unittest.TestCase):
    def open(self, owner=55, code=259, query_ok=True, handle=17):
        self.calls=[]
        def open_thread(access, inherit, tid):
            self.calls.append(('open',access,inherit,tid));return handle
        def get_owner(value):
            self.calls.append(('owner',value));return owner
        def exit_code(value, result):
            self.calls.append(('exit',value))
            C.cast(result,C.POINTER(C.c_uint32))[0]=code
            return query_ok
        def close(value): self.calls.append(('close',value))
        return open_target_thread(71,55,open_thread,get_owner,exit_code,close)

    def test_matching_live_handle_is_retained(self):
        self.assertEqual(self.open(),17)
        self.assertEqual(self.calls,[('open',0x4a,False,71),('owner',17),('exit',17)])

    def test_recycled_tid_is_closed_before_it_can_be_sampled(self):
        self.assertIsNone(self.open(owner=88))
        self.assertEqual(self.calls,[('open',0x4a,False,71),('owner',17),('close',17)])

    def test_failed_owner_query_is_closed(self):
        self.assertIsNone(self.open(owner=0))
        self.assertEqual(self.calls[-1],('close',17))

    def test_already_terminated_handle_is_closed(self):
        self.assertIsNone(self.open(code=0))
        self.assertEqual(self.calls[-1],('close',17))

    def test_failed_liveness_query_is_closed(self):
        self.assertIsNone(self.open(query_ok=False))
        self.assertEqual(self.calls[-1],('close',17))

    def test_failed_open_does_not_query_or_close(self):
        self.assertFalse(self.open(handle=None))
        self.assertEqual(self.calls,[('open',0x4a,False,71)])

    def test_terminated_cached_handle_discards_old_cycle_baseline(self):
        handles,previous,closed={71:17,72:18},{71:1000,72:2000},[]
        def exit_code(handle, result):
            C.cast(result,C.POINTER(C.c_uint32))[0]=0 if handle==17 else 259
            return True
        evict_terminated_threads(handles,previous,exit_code,closed.append)
        self.assertEqual(handles,{72:18})
        self.assertEqual(previous,{72:2000})
        self.assertEqual(closed,[17])
        # Reopening the same TID cannot inherit the terminated thread's cycles.
        handles[71]=self.open()
        self.assertNotIn(71,previous)


if __name__=='__main__':unittest.main()
