import math, sys
NSEG=800
def sg(x): return 1 if x>=0 else -1
def polar(R,ex,ey,terms,n,sup=1.0,rot=0.0):
    pts=[]
    for i in range(n):
        t=2*math.pi*i/n
        r=1+sum(a*math.cos(k*t+ph) for a,k,ph in terms)
        c,s=math.cos(t),math.sin(t)
        if sup!=1.0: c=sg(c)*abs(c)**sup; s=sg(s)*abs(s)**sup
        x,y=R*ex*r*c,R*ey*r*s
        cr,sr=math.cos(rot),math.sin(rot)
        pts.append((x*cr-y*sr,x*sr+y*cr))
    return pts
old=[(0,-180),(120,-170),(220,-80),(200,40),(120,90),(140,170),(40,230),(-80,200),(-150,120),(-120,20),(-220,-40),(-150,-140)]
old=[(x*1.6,y*1.6) for x,y in old]
defs=[
 ("rust",old,15),
 ("neon",polar(330,1.0,0.8,[(0.10,3,0.5)],28,sup=0.55),15),
 ("frozen",polar(300,1.9,0.7,[(0.14,3,1.0),(0.06,5,0)],26),16),
 ("swamp",polar(330,1.0,1.0,[(0.22,3,0),(0.10,2,1.0)],26),15),
 ("canyon",polar(340,1.2,0.9,[(0.2,4,0.3),(0.07,7,0)],30),16),
 ("volcano",polar(360,1.0,1.0,[(0.18,5,0.7),(0.08,2,0)],30),16),
 ("harbor",polar(330,1.6,0.8,[(0.12,3,2.0)],28,sup=0.65),17),
 ("moon",polar(340,1.3,0.9,[(0.28,1,0.4),(0.2,2,0.0)],26),16),
 ("jungle",polar(380,1.0,1.0,[(0.15,6,0),(0.2,3,1.0)],34),15),
 ("death",polar(400,1.2,1.0,[(0.3,2,0.5),(0.14,5,0),(0.06,7,1.0)],36),17),
]
def spline(P,D=12000):
    M=len(P);out=[]
    for i in range(D):
        u=i/D*M;s=int(u);f=u-s
        Q=[P[(s+k-1)%M] for k in range(4)]
        f2=f*f;f3=f2*f
        pt=[0.5*((2*Q[1][a])+(-Q[0][a]+Q[2][a])*f+(2*Q[0][a]-5*Q[1][a]+4*Q[2][a]-Q[3][a])*f2+(-Q[0][a]+3*Q[1][a]-3*Q[2][a]+Q[3][a])*f3) for a in (0,1)]
        out.append(pt)
    return out
def check(name,P,half):
    d=spline(P);D=len(d)
    cum=[0]
    for i in range(D): cum.append(cum[-1]+math.dist(d[i],d[(i+1)%D]))
    L=cum[D];tr=[];j=0
    for i in range(NSEG):
        t=L*i/NSEG
        while j<D-1 and cum[j+1]<t: j+=1
        tr.append(d[j])
    mind=1e9
    for i in range(0,NSEG,2):
        for k in range(i+1,NSEG,2):
            cd=min(k-i,NSEG-(k-i))
            if cd*L/NSEG<150: continue
            mind=min(mind,math.dist(tr[i],tr[k]))
    minr=1e9
    for i in range(NSEG):
        a,b,c=tr[(i-8)%NSEG],tr[i],tr[(i+8)%NSEG]
        ab,bc,ca=math.dist(a,b),math.dist(b,c),math.dist(c,a)
        ar=abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))
        if ar>1e-6: minr=min(minr,ab*bc*ca/(2*ar))
    ok = mind>2*half+8 and minr>half*1.5
    print(f"{name:8s} len={L:6.0f} gap={mind:6.1f} (need>{2*half+8}) minR={minr:5.1f} (need>{half*1.5:.0f}) {'OK' if ok else 'BAD'}",file=sys.stderr)
    return ok
allok=all([check(n,p,h) for n,p,h in defs])
if len(sys.argv)>1 and allok:
    with open(sys.argv[1],"w") as f:
        for i,(n,p,h) in enumerate(defs):
            f.write(f"static const float TP{i}[][2] = {{"+",".join("{%.0ff,%.0ff}"%(x,y) for x,y in p)+"};\n")
