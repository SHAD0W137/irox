struct CameraData {
    float4x4 view;
    float4x4 proj;
}; // Unused (for now)

struct PushConstants {
    column_major float4x4 mvp;
}; 

[[vk::push_constant]]
PushConstants pc;

[[vk::binding(0, 0)]]
Sampler2D baseColorTexture;


struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
    VSOutput output;
    output.position = mul(pc.mvp, float4(input.position, 1.0));
    output.normal = input.normal;
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    float3 lightDir = normalize(float3(0.5, 1.0, 0.7));
    float diffuse = max(dot(input.normal, lightDir), 0.2);

    float4 texColor = baseColorTexture.Sample(input.uv);

    return float4(texColor.rgb * diffuse, 1.0);
}