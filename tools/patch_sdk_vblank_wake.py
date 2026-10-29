"""ReXGlue v0.10.0 patch: the GPU command processor wakes on the guest vblank.

A flip waits in WAIT_REG_MEM for the vblank interrupt's memory write, polling with
Sleep(>= 1 ms), so presents landed 0-2 ms late on a 0.5 ms grid (frame times
15-18.5 ms at 60 FPS). MarkVblank now signals an event that the wait sleeps on,
with the original sleep as the timeout. Apply after patch_sdk_vblank_timer.py.

usage: patch_sdk_vblank_wake.py <rexglue-src>
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])
cp = root / 'src/graphics/command_processor.cpp'
gs = root / 'src/graphics/graphics_system.cpp'
cp_src, gs_src = cp.read_text(), gs.read_text()
if 'VblankWakeEvent' in cp_src:
    print('vblank wake: already patched')
    sys.exit(0)

cp_anchor = 'bool CommandProcessor::ExecutePacketType3_WAIT_REG_MEM('
cp_func = '''// Signaled by GraphicsSystem::MarkVblank after the vblank interrupt has run.
rex::thread::Event* VblankWakeEvent() {
  static auto event = rex::thread::Event::CreateAutoResetEvent(false);
  return event.get();
}

'''
cp_old = '''          rex::thread::Sleep(std::chrono::milliseconds(wait / 0x100));'''
cp_new = '''          rex::thread::Wait(VblankWakeEvent(), false,
                            std::chrono::milliseconds(wait / 0x100));'''
gs_old = '''  DispatchInterruptCallback(0, 2);
}'''
gs_new = '''  DispatchInterruptCallback(0, 2);
  VblankWakeEvent()->Set();
}'''
gs_decl_anchor = 'void GraphicsSystem::MarkVblank() {'
assert cp_src.count(cp_anchor) == 1 and cp_src.count(cp_old) == 1, 'command_processor anchors'
assert gs_src.count(gs_old) == 1 and gs_src.count(gs_decl_anchor) == 1, 'graphics_system anchors'
cp_src = cp_src.replace(cp_anchor, cp_func + cp_anchor).replace(cp_old, cp_new)
gs_src = gs_src.replace(gs_decl_anchor, 'rex::thread::Event* VblankWakeEvent();  // command_processor.cpp\n\n'
                        + gs_decl_anchor).replace(gs_old, gs_new)
cp.write_text(cp_src)
gs.write_text(gs_src)
print('vblank wake: patched')
