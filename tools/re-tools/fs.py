import pickle,bisect,sys,collections
L=pickle.load(open(__import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)),'dis.pkl'),'rb')); A=[x[0] for x in L]
calls=collections.defaultdict(list)
for a,m,o in L:
    if m=='call' and o.startswith('0x'): calls[int(o,16)].append(a)
def start(a):
    i=bisect.bisect_right(A,a)-1
    while i>0:
        p=L[i-1]
        if p[1] in('nop','int3') or (p[1]=='ret' and L[i][0] in calls) : 
            if L[i][0] in calls or p[1] in ('nop','int3'): return L[i][0]
        i-=1
for s in sys.argv[1:]:
    a=int(s,16); f=start(a)
    print(f"{a:X} in {f:X}, callers: "+" ".join(f"{c:X}" for c in calls[f][:10]))
