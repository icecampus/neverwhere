
// Track mouse movement change between frames and set camera distance and yaw.

#define PI 3.14159

const float maxCameraZoomDist = 20.;
const float minCameraZoomDist = -1.;
const vec2 cameraMoveScale = vec2(5.0, 5.0);


void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    if((fragCoord.x == 0.5) && (fragCoord.y < 4.0)){
    
        vec4 oldMouse = texelFetch(iChannel0, ivec2(0.5), 0).xyzw;
        vec4 mouse = (iMouse / iResolution.xyxy); 
        vec4 newMouse = vec4(0);
        
        float dist = texelFetch(iChannel0, ivec2(0.5, 1.5), 0).x;
        
        float mouseDownLastFrame = texelFetch(iChannel0, ivec2(0.5, 3.5), 0).x;
        
        // If mouse button is down and was down last frame
        if(iMouse.z > 0.0 && mouseDownLastFrame > 0.0){
            
            // Difference between mouse position last frame and now.
            vec2 mouseMove = mouse.xy-oldMouse.zw;
            newMouse = vec4(oldMouse.xy + cameraMoveScale * mouseMove, mouse.xy);
        }else{
            newMouse = vec4(oldMouse.xy, mouse.xy);
        }
        
         newMouse.x = mod(newMouse.x, 2.0*PI);
        //newMouse.y = min(0.99, max(-0.99, newMouse.y));


        // Store mouse data in the first pixel of Buffer B.
        if(fragCoord == vec2(0.5, 0.5)){
            // Set value at first frames
            if(iFrame < 5){
                mouse = vec4(1.15, 0.2, 0.0, 0.0);
            }
            fragColor = vec4(newMouse);
        }

        // Store camera distance in the second pixel of Buffer B.
        if(fragCoord == vec2(0.5, 1.5)){
            
            // Set value at first frames
            if(iFrame < 5){
                dist = 0.;
            }
            else
            {
                 // Set camera position from mouse information.
                 float mouseMove = newMouse.w-newMouse.y;
                 dist = mouseMove;
                 
                 dist = clamp(dist, minCameraZoomDist, maxCameraZoomDist);   
            }
        
             fragColor = vec4(dist, newMouse.x, 0.0, 0.0);
            
        }
        
         // Store whether the mouse button is down in the fourth pixel of Buffer A
        if(fragCoord == vec2(0.5, 3.5)){
            if(iMouse.z > 0.0){
            	fragColor = vec4(vec3(1.0), 1.0);
            }else{
            	fragColor = vec4(vec3(0.0), 1.0);
            }
        }

    }

}