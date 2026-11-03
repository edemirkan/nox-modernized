# generates the generic COM thunks: thunk i pushes i and jumps to the common forwarder
N=64
out=['.intel_syntax noprefix','.text']
for i in range(N):
    out+=[f'.globl _ddw_thunk{i}', f'_ddw_thunk{i}:', f'  push {i}', '  jmp ddw_common']
out+=['ddw_common:',
      '  push esp',            # sp -> [i][ret][this][args...]
      '  call _ddw_note',
      '  add esp, 4',
      '  pop eax',             # i
      '  mov ecx, [esp+4]',    # wrapper
      '  mov ecx, [ecx+4]',    # real object
      '  mov [esp+4], ecx',
      '  mov edx, [ecx]',
      '  jmp [edx+eax*4]',
      '.data','.globl _ddw_thunks','_ddw_thunks:']
out+=[f'  .long _ddw_thunk{i}' for i in range(N)]
open('thunks.S','w').write('\n'.join(out)+'\n')
