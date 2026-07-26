/*
    Blackbody flowing Lava 

    Based on:
    
    https://www.shadertoy.com/view/MdBSRW
    https://www.shadertoy.com/view/sdBGWh
    
    BufferA: Perlin noise FBM for lava
    BufferB: Camera change tracking
    
             
*/



#define NEW_LAVA 1            // Set to 1 to enable lava 
#define TEXUTRE_SHADING 1     // Set to 1 to enable sampled texuture for more real shading.
#define ROCK_TYPE 1           // Set 0 ~ 2 to show different rock 
#define CAMERA_TOP_VIEW 0    // Set 1 to enable camera fixed top view

#define MOTIONBLUR_EMBERS 	0 // Set to 1 to enable sampled motion blur on the embers.
#define ADD_HEAT_GLOW 		0 // Set to 1 to make the rock glow red as the lava covers it.

#define TEMPERATURE 2200.0

const float TEX_DETAIL = 0.5; 

float moveSpeed= .75;

float fbm(vec2 p);
float fizzerEmbers(vec3 norm, float time, vec2 coord);

//----------------------------- Texture distortion -----------------------------

float sampleLavaNoise(vec2 uv)
{
    return texture(iChannel1, uv).r;
}


// Find the local gradients in the X and Y directions which we use as the velocities 
// of the texure distortion
vec2 getGradient(vec2 uv){

    float scale = 0.1;
    float delta = 1e-1;
    
    uv *= scale;
    
    float data = sampleLavaNoise(uv);
    float gradX = data - sampleLavaNoise(uv-vec2(delta, 0.0));
    float gradY = data - sampleLavaNoise(uv-vec2(0.0, delta));
    
    return vec2(gradX, gradY);
}

// https://catlikecoding.com/unity/tutorials/flow/texture-distortion/
float getDistortedTexture(vec2 uv)
{
    float strength = 0.4;
    
    // The texture is distorted in time and we switch between two texture states.
    float time = 0.25 * iTime;

    float f = fract(time);
    
    // Get the velocity at the current location
    vec2 grad = getGradient(uv);
    vec2 distortion = strength * vec2(grad.x, grad.y);
    
    // Get two shifted states of the texture distorted in time by the local velocity.
    // Loop the distortion from 0 -> 1 using fract(time)
    
    float distort1 = sampleLavaNoise((uv + f * distortion));
    float distort2 = sampleLavaNoise(0.1 + uv + fract(time + 0.5) * distortion);

    // Mix between the two texture states to hide the sudden jump from 1 -> 0.
    // Modulate the value returned by the velocity to make slower regions darker in the final
    // lava render.
    return (1.0-length(grad)) * (mix(distort1, distort2, abs(1.0 - 2.0 * f)));
}


//----------------------------- Lava Shading -----------------------------


vec3 blackbody(float t)
{
    t *= TEMPERATURE;
    
    float u = ( 0.860117757 + 1.54118254e-4 * t + 1.28641212e-7 * t*t ) 
            / ( 1.0 + 8.42420235e-4 * t + 7.08145163e-7 * t*t );
    
    float v = ( 0.317398726 + 4.22806245e-5 * t + 4.20481691e-8 * t*t ) 
            / ( 1.0 - 2.89741816e-5 * t + 1.61456053e-7 * t*t );

    float x = 3.0*u / (2.0*u - 8.0*v + 4.0);
    float y = 2.0*v / (2.0*u - 8.0*v + 4.0);
    float z = 1.0 - x - y;
    
    float Y = 1.0;
    float X = Y / y * x;
    float Z = Y / y * z;

    mat3 XYZtoRGB = mat3(3.2404542, -1.5371385, -0.4985314,
                        -0.9692660,  1.8760108,  0.0415560,
                         0.0556434, -0.2040259,  1.0572252);

    return max(vec3(0.0), (vec3(X,Y,Z) * XYZtoRGB) * pow(t * 0.0004, 4.0));
}

// --- Shading Type 1

vec3 shadingSimple(vec3 p, vec3 norm, vec3 rd, float ph)
{
   // Base colour for the rocks.
    float f0=sqrt(fbm(p.xz*0.5));
    
    //return vec3(ph) *  mix(vec3(0.1),vec3(1.0,0.8,0.6)*0.3,f0);
    vec3 diffuse = mix(vec3(0.1,0.2,0.1)*0.5,mix(vec3(0.1),vec3(1.0,0.8,0.6)*0.3,f0),max(0.0,norm.y)) *mix(0.7,0.2,p.y)*mix(0.3,1.0,fbm(p.xz*3.0));
    
    diffuse*=(0.5+0.5*norm.x)*2.5+vec3(1.0,0.35,0.04)*0.02;
    
    
    float amb = clamp(0.4+0.6*norm.y,0.0,1.0);
    
    float spe = pow(clamp(dot(-rd,norm),0.0,1.0),16.0);
    
     //float occ = 1.;
    float occ = clamp(pow( p.y,1.)*1.,0.0,1.0);
    
    
    return diffuse *2. * amb *(0.2+0.8*occ) + 0.05* occ* spe*vec3(1.);
}


// --- Shading Simple Type 2

// Grey scale.
float getGrey(vec3 p){ return p.x*0.299 + p.y*0.587 + p.z*0.114; }

// Tri-Planar blending function. Based on an old Nvidia tutorial.
vec3 tex3D( sampler2D tex, in vec3 p, in vec3 n )
{
  
    n = max((abs(n) - 0.2)*7., 0.001); // n = max(abs(n), 0.001), etc.
    n /= (n.x + n.y + n.z ); 
	p = (texture(tex, p.yz)*n.x + texture(tex, p.zx)*n.y + texture(tex, p.xy)*n.z).xyz;
    return p*p;
}

vec3 doBumpMap( sampler2D tex, in vec3 p, in vec3 nor, float bumpfactor)
{
   
    const float eps = 0.001;
    vec3 grad = vec3( getGrey(tex3D(tex, vec3(p.x-eps, p.y, p.z), nor)),
                      getGrey(tex3D(tex, vec3(p.x, p.y-eps, p.z), nor)),
                      getGrey(tex3D(tex, vec3(p.x, p.y, p.z-eps), nor)));
    
    grad = (grad - getGrey(tex3D(tex,  p , nor)))/eps; 
            
    grad -= nor*dot(nor, grad);          
                      
    return normalize( nor + grad*bumpfactor );
	
}

vec3 shadingSimple_II(vec3 p, vec3 norm, vec3 ro, vec3 rd)
{
    vec3 shading = vec3(0.);
    // Light positioning. One is just in front of the camera, and the other is in front of that.
 	vec3 lp = ro - rd * 2.; // Put it a bit in front of the camera.
    
    
    // Texture scale factor.
    const float tSize0 = 1./3.;
    
     norm = doBumpMap(iChannel0, p*tSize0, norm, 0.02);
    
    // Obtaining the texel color. 
	vec3 texCol = tex3D(iChannel0, p*tSize0, norm);
    
    
    // Light direction vectors.
	vec3 ld = lp-p;
    
    // Distance from respective lights to the surface point.
	float lDist = max(length(ld), 0.001);
    
    // Normalize the light direction vectors.
	ld /= lDist;
    
    // Light attenuation, based on the distances above.
	float atten = 1./(1. + lDist*lDist*0.05);
    
    // Ambient light.
	float ambience = .1;
    
    // Diffuse lighting.
	float diff = max( dot(norm, ld), 0.0);
    
    // Specular lighting.
	float spec = pow(max( dot( reflect(-ld, norm), -rd ), 0.0 ), 16.);
    
    vec3 rCol = getGrey(texCol)*0.5 + texCol*0.5;
    
    float ao = clamp(pow(p.y, 1.5) * 1.5,0.0,1.0);
    
     // How much the fragment faces down
    float lava = max(dot(norm, vec3(0,-1,0)), 0.0);
    // A reddish light from directly below.
	vec3 lavaLight = lava * vec3(1, 0.1, 0.01);
    
    shading += (rCol * (diff * 1. +  ambience ) + lavaLight *0.2 + spec*texCol*0.1)*atten;
    
    shading *= ao;
    
    return shading;
}

// --- lava 

vec3 lava(vec3 norm, float lavaHeight, vec3 p, vec2 coord, float time, float moveSpeed, vec3 diffuse, vec3 ro, vec3 rd)
{
    float embers = fizzerEmbers(norm, time, coord);

    float mask =  max(0.0, 1.0 - abs(lavaHeight - p.y) * 16.);
    
    vec2 uv = p.xz - norm.yy * 0.2;//norm.yy - p.zx;
    
    uv += vec2(0., iTime * 0.05);
    uv.x -= mask * lavaHeight + p.y;

    //float tex = 1.15 - texture(iChannel0, uv).x;
    float tex = 1.05 - getDistortedTexture(uv * TEX_DETAIL);
    
    float hot = smoothstep(0.2, 0.0, lavaHeight);
    float cold = smoothstep(0.0, 1.0, (p.z + time * moveSpeed + sin(p.x + time * 0.2) + 1.0) * 0.3 + 0.1);
    
    float glow = max(0.0, (1.0-mask)*4.0 * (0.1 - (p.y - lavaHeight) *0.3));
    //float glow = max(0.0, (1.0-mask)*4.0 * (0.1 - (p.y - lavaHeight) *(f0*1.5 - 0.5) * f0));
    float heat_glow = smoothstep(0.0,3.0,p.z+time*moveSpeed) * max(0.0,1.0-p.y*1.5)*pow(3.0 * 0.4*(0.6*fbm(p.xz+vec2(time*0.5,0.0))+0.6*fbm(p.xz+vec2(-time*0.5,0.0))),3.0);
    //return vec3(heat_glow);
    glow = glow * 0.7 + 0.4 * heat_glow;
    
    float haze = length(ro-p) * 0.025 * cold;
    
    float temp = ((hot * 2.4 + 2.8) * tex - cold) * (tex+0.2);
    temp = mix(glow * 1.2, smoothstep(0.0, 1.5, temp) * 2.0, mask) + embers * 6.0;
    
    //return diffuse * (1. - mask);

    return diffuse * (1.0-mask)
                   + blackbody(temp) * vec3(2.6, 0.8, 0.5)
                   + haze * vec3(0.5,0.1,0.05);
}



float fizzerEmbers(vec3 norm, float time, vec2 coord)
{
    float embers=smoothstep(0.77+sin(time*20.0)*0.01+sin(time)*0.01,1.0,fbm(coord*10.0+vec2(cos(coord.y*0.8+time*0.7)*10.0,time*4.0)));
    embers+=smoothstep(0.77+sin(time*22.0)*0.01+sin(time*1.2)*0.01,1.0,fbm(vec2(100.0)+coord*8.0+vec2(time*8.0+cos(coord.y*0.3+time*0.3)*10.0,time*7.0)));
    return embers;
}

vec3 ACESFilm(vec3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}


// everything beyond this point is fizzer's original shader
// https://www.shadertoy.com/view/4djSzR

float cubic(float x)
{
    return (3.0 * x - 2.0 * x * x) * x;
}

vec3 rotateX(float angle, vec3 v)
{
    return vec3(v.x, cos(angle) * v.y + sin(angle) * v.z, cos(angle) * v.z - sin(angle) * v.y);
}

vec3 rotateY(float angle, vec3 v)
{
    return vec3(cos(angle) * v.x + sin(angle) * v.z, v.y, cos(angle) * v.z - sin(angle) * v.x);
}

float hash(float n)
{
    n=mod(n,1024.0);
    return fract(sin(n)*43758.5453);
}

float noise(vec2 p)
{
    return hash(p.x + p.y*57.0);
}

vec2 N22(vec2 p)
{
    float n = noise(p);
    return vec2(n, noise(p + n));
}


float smoothNoise2(vec2 p)
{
    vec2 p0 = floor(p + vec2(0.0, 0.0));
    vec2 p1 = floor(p + vec2(1.0, 0.0));
    vec2 p2 = floor(p + vec2(0.0, 1.0));
    vec2 p3 = floor(p + vec2(1.0, 1.0));
    vec2 pf = fract(p);
    return mix( mix(noise(p0), noise(p1), pf.x),mix(noise(p2), noise(p3), pf.x), pf.y);
}

float cellnoise(vec2 p)
{
    vec2 fp=fract(p);
    vec2 ip=vec2(floor( p ));
    float nd=1e3;
    vec2 nc=p;
    for(int i=-1;i<2;i+=1)
        for(int j=-1;j<2;j+=1)
        {
            vec2 c=ip+vec2(i,j)+vec2(noise(ip+vec2(i,j)),noise(ip+vec2(i+10,j)));
            float d=distance(c,p);
            if(d<nd)
            {
                nd=d;
                nc=c;
            }
        }

    return nd;
}

vec3 cellnoise2(in vec2 p)
{
    vec2 fp=fract(p);
    vec2 ip=vec2(floor( p ));
    float nd=1e3;
    vec2 nc=p;
    float id = 0.0;
    vec2 res = vec2( 100.0 );
    for(int i=-1;i<2;i+=1)
        for(int j=-1;j<2;j+=1)
        {
            vec2 b = vec2(i, j);
            vec2 c=ip+b+vec2(noise(ip+vec2(i,j)),noise(ip+vec2(i+10,j)));
            float d=distance(c,p);
            
            if( d < res.x )
            {
                id = dot( ip + b, vec2(59.0,213.0 ) );
                res = vec2( d, res.x );	
            }
             else if( d < res.y )
            {
                res.y = d;
            }
            
        }

    return vec3(sqrt(res), id);
}

// https://iquilezles.org/articles/voronoilines/
vec2 voronoiDistance( in vec2 x)
{
    vec2 p = vec2(floor( x ));
    vec2  f = fract( x );

    vec2 mb;
    vec2 mr;
    
    
    float id = 0.0;
    float res = 8.0;
    for( int j=-1; j<=1; j++ )
    for( int i=-1; i<=1; i++ )
    {
        vec2 b = vec2(i, j);
        vec2  r = vec2(b) + N22(p+b)-f;
        float d = dot(r,r);

        if( d < res )
        {
            id = dot( p+b, vec2(57.0,113.0 ) );
            res = d;
            mr = r;
            mb = b;
        }
    }

    res = 8.0;
    for( int j=-2; j<=2; j++ )
    for( int i=-2; i<=2; i++ )
    {
        vec2 b = mb + vec2(i, j);
        vec2  r = vec2(b) + N22(p+b) - f;
        float d = dot(0.5*(mr+r), normalize(r-mr));

        res = min( res, d );
    }

    return vec2(res, id);
}



float heightField(vec2 p)
{
    float H = 1.;
    float G = exp2(-H);
    float f = 4.;
    float a = 1.0;
    float t = 0.0;
    
    for( int i=0; i<3; i++ )
    {
        t += a*smoothNoise2(f*p);
        f *= 2.0;
        a *= G;
        
    }
    
    #if ROCK_TYPE == 2
    vec2 res = voronoiDistance( p * 0.8);
    float d = res.x;
    
    float tar = mix(0.1, 0.4, hash(res.y *2.63));
    //tar = 0.16;
    return smoothstep(0.0,tar,d) * 0.35 + t*0.1;
    #endif
    
    #if ROCK_TYPE == 1
    vec3 vt = cellnoise2(p * .8);
    float tar = mix(0.4, 1., hash(vt.z *5.78));
    //tar = 0.5;
    float d = clamp(3.5*(vt.y-vt.x), 0.0, 1.0 );
    return smoothstep(0.0,tar, d) * 0.34 + t*0.1;
    
    #else //
    return smoothstep(0.0,0.7,1.0-smoothstep(0.0,0.9,cellnoise(p)))*0.4+t*0.04;
    #endif
}



float fbm(vec2 p)
{
    float f=0.0;
    for(int i=0;i<4;i+=1)
        f+=smoothNoise2(p*exp2(float(i)))/exp2(float(i+1));
    return f;
}

float bumpHeight(vec2 p)
{
    float f=0.0;
    p*=4.0; // 4.0
    for(int i=0;i<5;i+=1)
        f+=smoothNoise2(p*exp2(float(i)))/exp2(float(i+1));
    return f * .5;
}

vec3 heightFieldBumpNormal(vec3 norm, vec2 p)
{
    vec2 eps=vec2(1e-5,0.0);
    float bumpScale=10.0;
    float c=bumpHeight(p);
    float d0=(bumpHeight(p+eps.xy))-c;
    float d1=(bumpHeight(p+eps.yx))-c;
    vec3 bn = normalize(cross(vec3(eps.y,d1,eps.x),vec3(eps.x,d0,eps.y)));
    
    return normalize(norm+(bn-norm*dot(norm,bn))*0.2);
}

vec3 heightFieldNormal(vec2 p)
{
    vec2 eps=vec2(1e-1,0.0);
    float bumpScale=10.0;
    float c=heightField(p);
    float d0=(heightField(p+eps.xy))-c;
    float d1=(heightField(p+eps.yx))-c;
    vec3 n0 = normalize(cross(vec3(eps.y,d1,eps.x),vec3(eps.x,d0,eps.y)));
    return n0;
    //vec3 bn = bumpNormal(p);
    //return normalize(n0+ bn);
    //return normalize(n0+(bn-n0*dot(n0,bn))*0.2);
}


vec3 tonemap(vec3 c)
{
    return c/(c+vec3(0.6));
}

float evalLavaHeight(vec2 p)
{
    float off = 0.;
    
    #if CAMERA_TOP_VIEW
    off = 3.;
    #endif 
    
    float h0 = getDistortedTexture(p * TEX_DETAIL) * 1.2;
    h0 = mix(0.2,0.0, clamp(h0 * h0 *  h0, 0., 1.));
    return mix(-0.5, h0, cubic(clamp(p.y + iTime*moveSpeed - off + sin(p.x+iTime*0.2), 0., 1.)));
    //return mix(-0.5, h0, cubic(clamp(p.y + iTime*moveSpeed + sin(p.x+iTime*0.2), 0., 1.))); 
}

vec3 shadingDefault(vec3 p, vec3 norm, vec3 ro, vec3 rd, float lavaHeight, vec2 coord)
{
    float f0=sqrt(fbm(p.xz*0.5));
    
    vec3 diffuse=mix(vec3(0.1,0.2,0.1)*0.5,mix(vec3(0.1),vec3(1.0,0.8,0.6)*0.3,f0),max(0.0,norm.y))*mix(0.7,0.2,p.y)*mix(0.3,1.0,fbm(p.xz*3.0));
 

    // Cheating by simply adding light from the lava into the diffuse albedo.
    diffuse+=vec3(1.0,0.35,0.04)*clamp((1.0-norm.y)*0.1+pow(max(0.0,(1.0-abs(lavaHeight-p.y)*4.0)),2.0),0.0,1.0)*0.4;
    diffuse=mix(1.5*vec3(1.0,0.35,0.04),diffuse,clamp((p.y-lavaHeight)*16.0,0.0,1.0));
    
    
    
#if ADD_HEAT_GLOW
    vec3 glow=smoothstep(0.0,3.0,p.z+iTime*moveSpeed)*max(0.0,1.0-p.y*1.5)*pow(3.0*vec3(0.4,0.21,0.1)*(0.6*fbm(p.xz+vec2(iTime*0.5,0.0))+0.6*fbm(p.xz+vec2(-iTime*0.5,0.0))),vec3(3.0));
#else
    vec3 glow=vec3(0.0);
#endif
    
    // Some small bright bits for fake embers to suggest fire.
#if MOTIONBLUR_EMBERS
    vec3 embers=vec3(0.0);
    for(int j=0;j<8;j+=1)
    {
        float mb_time=iTime+float(j)*6e-2/8.0;
	    embers+=vec3(1.0,0.35,0.04)*smoothstep(0.77+sin(mb_time*20.0)*0.01+sin(mb_time)*0.01,1.0,fbm(coord*10.0+vec2(cos(coord.y*0.8+mb_time*0.7)*10.0,mb_time*4.0)));
    	embers+=vec3(1.0,0.35,0.04)*smoothstep(0.77+sin(mb_time*22.0)*0.01+sin(mb_time*1.2)*0.01,1.0,fbm(vec2(100.0)+coord*8.0+vec2(mb_time*8.0+cos(coord.y*0.3+mb_time*0.3)*10.0,mb_time*7.0)));
    }
    embers/=8.0*0.5;
#else
    vec3 embers=vec3(1.0,0.35,0.04)*smoothstep(0.77+sin(iTime*20.0)*0.01+sin(iTime)*0.01,1.0,fbm(coord*10.0+vec2(cos(coord.y*0.8+iTime*0.7)*10.0,iTime*4.0)));
    embers+=vec3(1.0,0.35,0.04)*smoothstep(0.77+sin(iTime*22.0)*0.01+sin(iTime*1.2)*0.01,1.0,fbm(vec2(100.0)+coord*8.0+vec2(iTime*8.0+cos(coord.y*0.3+iTime*0.3)*10.0,iTime*7.0)));

#endif
    
    // Wrap lighting is applied here, both to the rock, lava, and glow from lava. This is not correct, but
    // it gives some substance to the lava and variation/shadow to the glow. 
    return diffuse*(0.5+0.5*norm.x)*2.5+vec3(1.0,0.35,0.04)*0.02+embers+glow;
}


vec3 samplef(vec2 coord)
{
    // Set up ray.
    vec4 cameraSetting = texelFetch(iChannel2, ivec2(0.5, 1.5), 0);
    float cam_dist = cameraSetting.x;
    float cam_rotateY = cameraSetting.y;
    
    #if CAMERA_TOP_VIEW
    vec3 ro=vec3(2.,4.+ cam_dist,0.1);
    vec3 rd=rotateY(3.1415926 - cam_rotateY,rotateX(3.1415926/2.1,normalize(vec3(coord,-1.3))));
    #else
    vec3 ro=vec3(0.0, 3.0 + cam_dist,-2.0-iTime*moveSpeed+cos(iTime*1.0)*0.05);
    vec3 rd=rotateY(3.1415926+sin(iTime*0.1) - cam_rotateY,rotateX(1.0+sin(iTime*0.4)*0.05,normalize(vec3(coord,-1.3))));
    #endif

    // Intersect the ray with the upper and lower planes of the heightfield.
    float t0=(0.5-ro.y)/rd.y;
    float t1=(0.0-ro.y)/rd.y;

    const int n= 32;

    float lavaHeight=0.0;

    vec3 prevp=ro+rd*t0,p=prevp;
    float ph=heightField(prevp.xz);
    
    // Raymarch through the heightfield with a fixed number of steps.
    for(int i=1;i<n;i+=1)
    {
        float pt=mix(t0,t1,float(i-1)/float(n));
        float t=mix(t0,t1,float(i)/float(n));
        p=ro+rd*t;
        lavaHeight=evalLavaHeight(p.xz);
        float h=max(lavaHeight,heightField(p.xz));
        if(h>p.y)
        {
            // Refine the intersection point.
            float lrd=length(rd.xz);
            vec2 v0=vec2(lrd*pt, prevp.y);
            vec2 v1=vec2(lrd*t, p.y);
            vec2 v2=vec2(lrd*pt, ph);
            vec2 dv=vec2(h-v2.y,v2.x-v1.x);
            float inter=dot(v2-v0,dv)/dot(v1-v0,dv);
            p=mix(prevp,p,inter);

            // Re-evaluate the lava height using the refined intersection point.
            lavaHeight=evalLavaHeight(p.xz);
            break;
        }
        prevp=p;
        ph=h;
    }
    
    vec3 norm= heightFieldNormal(p.xz);
  
    vec3 shadingColor = vec3(0.);

#if NEW_LAVA

    #if TEXUTRE_SHADING
    shadingColor = shadingSimple_II(p, norm, ro, rd);
    #else 
    vec3 bnorm = heightFieldBumpNormal(norm, p.xz);
    shadingColor = shadingSimple(p, bnorm, rd, ph);
    #endif
    
    return lava(norm, lavaHeight, p, coord, iTime, moveSpeed, shadingColor, ro, rd);
    
#else
    vec3 bnorm = heightFieldBumpNormal(norm, p.xz);
    return shadingDefault(p, bnorm, ro, rd, lavaHeight, coord);
#endif

}


void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    // Sample the scene, with a distorted coordinate to simulate heat haze.
    vec2 uv = fragCoord.xy / iResolution.xy;
    uv = (uv - vec2(0.5)) * 2.0;
    uv.x *= iResolution.x / iResolution.y;
    fragColor.rgb=samplef(uv+vec2(cos(smoothNoise2(vec2(-iTime*10.0+uv.y*10.0,uv.x)))*0.01,0.0));
    
#if NEW_LAVA && TEXUTRE_SHADING != 0
    fragColor.rgb=ACESFilm(fragColor.rgb);
    
    // Subtle vignette.
    uv = fragCoord/iResolution.xy;
    fragColor.rgb *= min(pow(16.*(1. - uv.x)*(1. - uv.y)*uv.x*uv.y, 1./8.)*1.1, 1.);
    
     // Gamma
    fragColor = vec4(sqrt(clamp(fragColor.rgb, 0., 1.)), 1.0);
#else 
    fragColor.rgb=tonemap(fragColor.rgb) * 1.2;
#endif 
    
    //fragColor.rgb = texture(iChannel1, fragCoord.xy / iResolution.xy).rgb;
}
