#version 440

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

// uniform block: 84 bytes
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;         // offset 0
    float qt_Opacity;       // offset 64
    float zoom;             // offset 68
    vec2 center;            // offset 72
    int limit;              // offset 80
    float iTime;
} ubuf;


void main()
{


   // Нормализация координат (аналог fragCoord.xy/iResolution.xy)
    vec2 uv = vTexCoord;
    
    // Фоновый цвет (как в оригинале)
    vec4 texture_color = vec4(0.192156862745098, 0.6627450980392157, 0.9333333333333333, 1.0);
    
    // Анимационные вычисления
    vec4 k = vec4(ubuf.iTime) * 0.8;
    k.xy = uv * 700.0;
    
    // Матричные преобразования
    mat3 m1 = mat3(
        vec3(-2.0, -1.0, 0.0),
        vec3(3.0, -1.0, 1.0),
        vec3(1.0, -1.0, -1.0)
    ) * 0.5;
    
    mat3 m2 = mat3(
        vec3(-2.0, -1.0, 0.0),
        vec3(3.0, -1.0, 1.0),
        vec3(1.0, -1.0, -1.0)
    ) * 0.2;
    
    mat3 m3 = mat3(
        vec3(-2.0, -1.0, 0.0),
        vec3(3.0, -1.0, 1.0),
        vec3(1.0, -1.0, -1.0)
    ) * 0.5;

    // Вычисления значений
    float val1 = length(0.5 - fract(k.xyw *= m1));
    float val2 = length(0.5 - fract(k.xyw *= m2));
    float val3 = length(0.5 - fract(k.xyw *= m3));
    
    // Финальный цвет
    vec4 color = vec4(pow(min(min(val1, val2), val3), 7.0) * 3.0) + texture_color;
    fragColor = color;



    // vec4 color1 = vec4(1.0, 0.85, 0.55, 1);
    // vec4 color2 = vec4(0.226, 0.0, 0.615, 1);

    // float aspect_ratio = -ubuf.qt_Matrix[0][0]/ubuf.qt_Matrix[1][1];
    // vec2 z, c;

    // c.x = (vTexCoord.x - 0.5) / ubuf.zoom + ubuf.center.x;
    // c.y = aspect_ratio * (vTexCoord.y - 0.5) / ubuf.zoom + ubuf.center.y;

    // int iLast;
    // z = c;
    // for (int i = 0; i < 1000000; i++) {
    //     if (i >= ubuf.limit)
    //     {
    //         iLast = i;
    //         break;
    //     }
    //     float x = (z.x * z.x - z.y * z.y) + c.x;
    //     float y = (z.y * z.x + z.x * z.y) + c.y;

    //     if ((x * x + y * y) > 4.0)
    //     {
    //         iLast = i;
    //         break;
    //     }
    //     z.x = x;
    //     z.y = y;
    // }

    // if (iLast == ubuf.limit) {
    //     fragColor = vec4(0.0, 0.0, 0.0, 1.0);
    // } else {
    //     float f = (iLast * 1.0) / ubuf.limit;
    //     fragColor = mix(color1, color2, sqrt(f));
    // }
}
