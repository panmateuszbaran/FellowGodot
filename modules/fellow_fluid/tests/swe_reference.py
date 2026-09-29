"""NumPy port of modules/fellow_fluid/fellow_swe.glsl (same staggered scheme, limiter and wet/dry rules).
Used to validate stability and exact mass conservation of the GPU solver on a dam break with obstacles.
Run: python modules/fellow_fluid/tests/swe_reference.py   (expects "mass 1.00000" on every line, no NaN)
"""
# NumPy port of modules/fellow_fluid/fellow_swe.glsl (same equations) to sanity-check stability/mass.
import numpy as np
N=128; dx=64.0/N; dt=1/120; g=9.81; damp=0.05; solid_h=20.0; max_speed=1.0*dx/dt
yy,xx=np.meshgrid(np.arange(N),np.arange(N),indexing='ij')  # [j,i]
B=np.zeros((N,N)); B[(xx-40)**2+(yy-64)**2<36]=50.0  # pillar
B+= 0.3*np.exp(-((xx-90)**2+(yy-40)**2)/200)          # island bump
h=np.maximum(0.8-B,0); h[:, :30]+=0.6*(B[:, :30]<solid_h)  # dam break
u=np.zeros((N,N)); v=np.zeros((N,N)); foam=np.zeros((N,N))
solid=B>=solid_h; h[solid]=0
def S(a,di,dj):  # a[j+dj, i+di] clamped
    J=np.clip(yy+dj,0,N-1); I=np.clip(xx+di,0,N-1); return a[J,I]
def solid_at(di,dj):
    J=yy+dj; I=xx+di; out=(J<0)|(J>=N)|(I<0)|(I>=N)
    return out | solid[np.clip(J,0,N-1),np.clip(I,0,N-1)]
def samp(a,px,py,ox,oy):
    qx=px-ox; qy=py-oy; fx=np.floor(qx); fy=np.floor(qy); tx=qx-fx; ty=qy-fy
    I=np.clip(fx.astype(int),0,N-1); J=np.clip(fy.astype(int),0,N-1); I1=np.clip(I+1,0,N-1); J1=np.clip(J+1,0,N-1)
    return (a[J,I]*(1-tx)+a[J,I1]*tx)*(1-ty)+(a[J1,I]*(1-tx)+a[J1,I1]*tx)*ty
def step():
    global h,u,v,foam
    k=dt/dx
    vu=0.25*(v+S(v,1,0)+S(v,0,-1)+S(v,1,-1)); un=samp(u,xx+1-k*u,yy+0.5-k*vu,1,0.5)
    uv=0.25*(u+S(u,-1,0)+S(u,0,1)+S(u,-1,1)); vn=samp(v,xx+0.5-k*uv,yy+1-k*v,0.5,1)
    uc=0.5*(u+S(u,-1,0)); vc=0.5*(v+S(v,0,-1)); fn=samp(foam,xx+0.5-k*uc,yy+0.5-k*vc,0.5,0.5)
    u,v,foam=un,vn,fn
    # raw fluxes through +X / +Z faces (zero on domain boundary faces)
    Fx=u*np.where(u>0,h,S(h,1,0)); Fz=v*np.where(v>0,h,S(h,0,1))
    Fx[:, N-1]=0; Fz[N-1, :]=0
    Fxl=np.where(xx>0,S(Fx,-1,0),0); Fzl=np.where(yy>0,S(Fz,0,-1),0)   # flux through -X / -Z faces
    out=np.maximum(Fx,0)+np.maximum(Fz,0)+np.maximum(-Fxl,0)+np.maximum(-Fzl,0)
    alpha=np.where(out*k>h, h/np.maximum(out*k,1e-12), 1.0)
    # face flux scaled by the upwind (source) cell's alpha
    Fxs=Fx*np.where(Fx>0,alpha,S(alpha,1,0)); Fzs=Fz*np.where(Fz>0,alpha,S(alpha,0,1))
    div=Fxs-np.where(xx>0,S(Fxs,-1,0),0)+Fzs-np.where(yy>0,S(Fzs,0,-1),0)
    h=h-k*div; assert h.min() > -1e-9, 'negative depth'; h=np.maximum(h,0); h[solid]=0
    e=h+B
    def face(vel,di,dj):
        h0=h; h1=S(h,di,dj); e0=e; e1=S(e,di,dj); B0=B; B1=S(B,di,dj)
        vel=vel-g*k*(e1-e0)
        vel=np.where(h0<=1e-4,np.minimum(vel,0),vel); vel=np.where(h1<=1e-4,np.maximum(vel,0),vel)
        vel=np.where(((h1<=1e-4)&(B1>e0))|((h0<=1e-4)&(B0>e1)),0,vel)
        vel=vel*np.exp(-damp*dt); vel=np.clip(vel,-max_speed,max_speed)
        vel=np.where((h0<=1e-4)&(h1<=1e-4),0,vel)
        vel=np.where(solid|solid_at(di,dj),0,vel)
        return vel
    u=face(u,1,0); v=face(v,0,1)
m0=h.sum()
for t in range(1200):
    step()
    if t%200==0 or t==1199:
        print(t, "mass %.5f"%(h.sum()/m0), "max|u| %.2f"%np.abs(u).max(), "hmax %.3f"%h.max(), "nan", np.isnan(h).any())
