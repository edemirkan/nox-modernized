import pickle,sys,bisect
L=pickle.load(open(__import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)),'dis.pkl'),'rb')); A=[x[0] for x in L]
a=int(sys.argv[1],16); n=int(sys.argv[2]) if len(sys.argv)>2 else 40
i=bisect.bisect_left(A,a)
for x in L[i:i+n]: print(f"{x[0]:08X}  {x[1]} {x[2]}")
