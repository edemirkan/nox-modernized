# Linear disassembly of Game.exe's .text into dis.pkl: a list of (address, mnemonic, operands).
# python3 mkdis.py path\to\Game.exe   (pip install capstone pefile)
import pickle, sys, os, pefile, capstone
pe = pefile.PE(sys.argv[1], fast_load=True)
img = pe.get_memory_mapped_image(); base = pe.OPTIONAL_HEADER.ImageBase
text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
a0 = base + text.VirtualAddress; n = text.Misc_VirtualSize
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); md.skipdata = True
out = [(i.address, i.mnemonic, i.op_str) for i in md.disasm(img[a0 - base:a0 - base + n], a0)]
pickle.dump(out, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'dis.pkl'), 'wb'))
print(len(out), 'instructions')
