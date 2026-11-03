import pickle,sys,re
L=pickle.load(open(__import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)),'dis.pkl'),'rb'))
lo,hi=int(sys.argv[1],16),int(sys.argv[2],16)
for a,m,o in L:
    for h in re.findall(r'0x[0-9a-f]+',o):
        v=int(h,16)
        if lo<=v<hi and m not in('call','jmp') and not m.startswith('j'): print(f"{a:08X} {m} {o}")
