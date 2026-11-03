import pickle,sys,collections
L=pickle.load(open(__import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)),'dis.pkl'),'rb'))
lo,hi=int(sys.argv[1],16),int(sys.argv[2],16)
c=collections.defaultdict(list)
for a,m,o in L:
    if m in('call','jmp') and o.startswith('0x'):
        t=int(o,16)
        if lo<=t<hi: c[t].append(a)
for t in sorted(c): print(f"{t:08X} <- {len(c[t])}: "+" ".join(f"{x:X}" for x in c[t][:12]))
