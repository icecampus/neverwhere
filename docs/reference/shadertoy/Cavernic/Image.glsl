
// Cavernic by Leon Denise 2023-01-17

// a noise designed cavern with rock and water
// (you can move camera with mouse)

// globals
float material, total;

// snippets
#define R iResolution
#define N(x,y,z) normalize(vec3(x,y,z))
#define ss(a,b,t) smoothstep(a,b,t)
#define repeat(p,r) (mod(p,r)-r/2.)
mat2 rot(float a) { return mat2(cos(a),-sin(a),sin(a),cos(a)); }
float gyroid(vec3 p) { return dot(sin(p), cos(p.yzx)); }

// Victor Shepardson + Inigo Quilez 
// https://www.shadertoy.com/view/XlXcW4
const uint k = 1103515245U;  // GLIB C
vec3 hash( uvec3 x )
{
    x = ((x>>8U)^x.yzx)*k;
    x = ((x>>8U)^x.yzx)*k;
    x = ((x>>8U)^x.yzx)*k;
    return vec3(x)*(1.0/float(0xffffffffU));
}

// Dave Hoskins
// https://www.shadertoy.com/view/4djSRW
float hash12(vec2 p)
{
	vec3 p3  = fract(vec3(p.xyx) * .1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// the noises
float noise(inout vec3 p)
{
    float result = 0., a = .5;
    for (float i = 0.; i < 3.; ++i, a/=2.)
    {
        result += (gyroid(p/a)*a);
    }
    return result;
}

float noise2(vec3 p)
{
    float result = 0., a = .5;
    for (float i = 0.; i < 6.; ++i, a/=2.)
    {
        p.z += result * .5;
        result += abs(gyroid(p/a)*a);
    }
    return result;
}

float noise3(vec3 p)
{
    float result = 0., a = .5;
    for (float i = 0.; i < 5.; ++i, a/=2.)
    {
        p.y += result * .5 + iTime * .05;
        result += abs(gyroid(p/a)*a);
    }
    return result;
}

float noise4(vec3 p)
{
    float result = 0., a = .5;
    for (float i = 0.; i < 3.; ++i, a/=2.)
    {
        p.y += result * .5;
        result += abs(gyroid(p/a)*a);
    }
    return result;
}

float map(vec3 p)
{
    float dist = 100.;
    
    // recenter
    p.x += .7;
    
    // travel
    p.z -= iTime * .1;
    
    // save position for later
    vec3 q = p;
    
    // global structure
    p.z *= .5;
    dist = noise(p);
    
    // subtract medium holes
    float grid = .5;
    float shape = length(repeat(p,grid))-grid/1.5;
    shape = max(dist, abs(shape)-.1);
    dist = max(dist, -abs(shape)*.5);
    
    // add intermediate structure
    p = q*5.;
    p.y *= .3;
    dist += abs(noise(p))*.2;
    
    // add medium vertical details
    p = q*10.;
    p.y *= .2;
    dist += pow(abs(noise(p)), 4.)*.1;
    
    // extra medium horizontal details
    p = q;
    p.y += cos(p.z*2.)*.05;
    p.zx *= .3;
    dist -= pow(abs(noise4(p*10.)), 4.)*.03;
    
    // add surface details
    p = q*10.;
    p.z *= 2.;
    dist -= noise2(p) * .05;
    
    // inflate/deflate volume along z
    dist -= .1;
    dist -= .1 * sin(q.z);
    
    // inflate volume for the ceiling
    dist -= max(0., p.y) * .02;
    
    // water
    float water = q.y + 1. + noise3(q*2.) * .01;
    
    material = water < dist ? 1. : 0.;
    dist = min(water, dist);
    
    return dist;
}

// NuSan
// https://www.shadertoy.com/view/3sBGzV
vec3 getNormal(vec3 pos, float e)
{
    vec2 noff = vec2(e,0);
    return normalize(map(pos)-vec3(map(pos-noff.xyy), map(pos-noff.yxy), map(pos-noff.yyx)));
}

vec3 getColor(vec3 pos, vec3 normal, vec3 ray, float shade)
{
    // Inigo Quilez palette
    // https://iquilezles.org/articles/palettes
    vec3 color = .5+.5*cos(vec3(1,2,3)*5.9+normal.y-normal.z*.5-.5);
    
    // light
    color *= dot(normal, -normalize(pos))*.5+.5;
    
    // shadow
    color *= shade*shade;
    
    return color;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{

    vec3 color = vec3(0);
    
    // coordinates
    vec2 uv = fragCoord/iResolution.xy;
    vec2 p = (2.*fragCoord-R.xy)/R.y;
    vec3 pos = vec3(0,0,0);
    vec3 ray = normalize(vec3(p,-1.));
    vec3 rng = hash(uvec3(fragCoord, 0.));
    
    // mouse camera
    bool clicked = iMouse.x > 0.;
    bool clicking = iMouse.z > 0.;
    if (clicked)
    {
        vec2 mouse = iMouse.xy-abs(iMouse.zw)+R.xy/2.;
        vec2 angle = vec2((2.*mouse-R.xy)/R.y);
        ray.yz *= rot(angle.y);
        ray.xz *= rot(angle.x);
    }

    // raymarch
    total = 0.;
    float shade = 0.;
    for (shade = 1.; shade > 0.; shade -= 1./200.)
    {
        float dist = map(pos);
        if (dist < .001*total || total > 20.) break;
        dist *= 0.12 + 0.05*rng.z;
        pos += ray * dist;
        total += dist;
    }

    // coloring
    if (shade > .01)
    {
        float mat = material;
        vec3 normal = getNormal(pos, .003*total);
        
        // cavern
        if (mat == 0.)
        {
            color = getColor(pos, normal, ray, shade);
            
            // water wet
            float spec = pow(dot(-ray, normal)*.5+.5, 100.);
            color += .2*spec*ss(.5,.0,pos.y+1.);
        }
        // water
        else
        {
            // raymarch reflection
            ray = reflect(ray, normal);
            pos += ray *.05;
            total = 0.;
            for (shade = 1.; shade > 0.; shade -= 1./80.)
            {
                float dist = map(pos);
                if (dist < .05*total || total > 20.) break;
                dist *= 0.2;
                pos += ray * dist;
                total += dist;
            }
            
            // color reflection
            color = getColor(pos, getNormal(pos, .001), ray, shade);
            color *= ss(1.,0.,pos.y+1.);
            color *= ss(0.,0.6,(pos.y+1.2));
        }
    }
    
    fragColor = vec4(color,1.0);
}