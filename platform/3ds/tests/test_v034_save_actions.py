import importlib.util
import json
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


class SaveActionTests(unittest.TestCase):
    def test_action_transitions_require_explicit_confirmation(self):
        script = (ROOT / 'wadsrc/static/zscript/engine/ui/menu/loadsavemenu.zs').read_text()
        body = script.split('private bool NativeSaveEvent(int mkey)', 1)[1].split('\n\toverride bool MouseEvent', 1)[0]
        cpp = r'''
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>
using std::max; using std::min;
enum { MKEY_Back, MKEY_Up, MKEY_Down, MKEY_Left, MKEY_Right, MKEY_Enter, MKEY_MBYes };
struct Node { std::string Filename; };
struct Manager {
    std::vector<Node> saves{{"first"},{"second"}};
    bool fail=false;
    int loads=0, deletes=0;
    int SavegameCount() { return saves.size(); }
    Node GetSavegame(int index) { return saves.at(index); }
    void LoadSavegame(int index) { assert(index==1); ++loads; }
    int RemoveSaveSlot(int index) {
        ++deletes;
        if (fail) return index;
        saves.erase(saves.begin()+index);
        return min(index,SavegameCount()-1);
    }
};
struct Menu {
    Manager manager;
    int NativeActionStage=1, NativeActionChoice=0, Selected=1, TopItem=1;
    std::string NativeActionFile="second";
    void UpdateSaveComment() {}
    bool NativeSaveEvent(int mkey)
'''
        cpp += body + r'''
};
int main() {
    Menu load;
    assert(load.manager.loads==0);
    load.NativeSaveEvent(MKEY_Enter);
    assert(load.manager.loads==1 && load.manager.deletes==0);
    Menu cancel;
    cancel.NativeSaveEvent(MKEY_Down);
    cancel.NativeSaveEvent(MKEY_Enter);
    assert(cancel.NativeActionStage==2 && cancel.NativeActionChoice==1);
    cancel.NativeSaveEvent(MKEY_Enter);
    assert(cancel.NativeActionStage==1 && cancel.manager.deletes==0);
    cancel.NativeSaveEvent(MKEY_Enter);
    cancel.NativeSaveEvent(MKEY_Back);
    assert(cancel.NativeActionStage==1 && cancel.manager.deletes==0);
    cancel.NativeSaveEvent(MKEY_Back);
    assert(cancel.NativeActionStage==0 && cancel.manager.deletes==0);
    Menu confirmed;
    confirmed.NativeSaveEvent(MKEY_Down);
    confirmed.NativeSaveEvent(MKEY_Enter);
    confirmed.NativeSaveEvent(MKEY_MBYes);
    assert(confirmed.manager.deletes==0);
    confirmed.NativeSaveEvent(MKEY_Up);
    confirmed.NativeSaveEvent(MKEY_Enter);
    assert(confirmed.manager.deletes==1 && confirmed.NativeActionStage==0);
    assert(confirmed.manager.saves[0].Filename=="first" && confirmed.TopItem==0);
    Menu changed;
    changed.NativeActionStage=2;
    changed.Selected=0;
    changed.NativeSaveEvent(MKEY_Enter);
    assert(changed.manager.deletes==0 && changed.NativeActionStage==0);
    Menu failed;
    failed.NativeActionStage=2;
    failed.manager.fail=true;
    failed.NativeSaveEvent(MKEY_Enter);
    assert(failed.NativeActionStage==3 && failed.manager.SavegameCount()==2);
    failed.NativeSaveEvent(MKEY_Enter);
    assert(failed.NativeActionStage==0 && failed.manager.deletes==1);
    Menu last;
    last.manager.saves={{"second"}}; last.Selected=0;
    last.NativeActionStage=2;
    last.NativeSaveEvent(MKEY_Enter);
    assert(last.Selected==-1 && last.TopItem==0 && last.NativeActionStage==0);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'actions.cpp'
            binary = Path(directory) / 'actions'
            source.write_text(cpp)
            subprocess.run([shutil.which('c++'), '-std=c++17', str(source), '-o', str(binary)], check=True, capture_output=True)
            subprocess.run([str(binary)], check=True, capture_output=True)

    def test_banner_scale_preserves_center_and_non_position_data(self):
        path = ROOT / 'platform/3ds/tools/scale-banner-glb.py'
        spec = importlib.util.spec_from_file_location('scale_banner', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        positions = [(-2, -1, -2), (2, 3, 2), (0, 1, 0)]
        binary = b''.join(struct.pack('<3f', *p) for p in positions) + b'UVS!'
        model = {'nodes': [{'mesh': 0}], 'meshes': [{'primitives': [{'attributes': {'POSITION': 0}}]}],
                 'bufferViews': [{'buffer': 0, 'byteLength': 36}],
                 'accessors': [{'bufferView': 0, 'componentType': 5126, 'count': 3, 'type': 'VEC3'}]}
        encoded = json.dumps(model).encode()
        encoded += b' ' * (-len(encoded) % 4)
        data = (struct.pack('<4sII', b'glTF', 2, 28 + len(encoded) + len(binary)) +
                struct.pack('<I4s', len(encoded), b'JSON') + encoded +
                struct.pack('<I4s', len(binary), b'BIN\0') + binary)
        result = module.scale_glb(data, .85)
        size = struct.unpack_from('<I', result, 12)[0]
        points = result[28 + size:]
        self.assertEqual(points[-4:], b'UVS!')
        for i, point in enumerate(positions):
            for actual, old, center in zip(struct.unpack_from('<3f', points, i * 12), point, (0, 1, 0)):
                self.assertAlmostEqual(actual, center + (old - center) * .85, places=6)
