#pragma once
// =============================================================================
// Shaders.h - HLSL 소스
//   kShaderSource      : 씬 그리기 (박스, 칠판 면)
//   kBrushShaderSource : ★ [4단계] 칠판 텍스처에 도장 찍기
// =============================================================================

inline constexpr char kShaderSource[] = R"HLSL(

cbuffer ObjectCB : register(b0)
{
    float4x4 gWorld;
    float4   gColor;
    float4   gUVRect;  // ★ [7-2단계] 라벨 아틀라스 영역 (xy 시작, zw 크기)
    float4   gParams;  // ★ [7-2단계] x = 하이라이트 (0~1)
};

cbuffer FrameCB : register(b1)
{
    float4x4 gViewProj;
    float4   gLightDir;
    float4   gCursor;
    float4   gCursorColor;
};

Texture2D    gBoardTex : register(t0);
Texture2D    gLabelTex : register(t1);  // ★ [7-2단계] 버튼 라벨 아틀라스
SamplerState gSampler  : register(s0);

struct VSIn
{
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD;
};

struct VSOut
{
    float4 posH    : SV_POSITION;
    float3 normalW : NORMAL;
    float2 uv      : TEXCOORD;
};

VSOut VSMain(VSIn vin)
{
    VSOut o;
    float4 posW = mul(float4(vin.pos, 1.0f), gWorld);
    o.posH      = mul(posW, gViewProj);
    o.normalW   = mul(vin.normal, (float3x3)gWorld);
    o.uv        = vin.uv;
    return o;
}

float Lighting(float3 normalW)
{
    float3 n       = normalize(normalW);
    float3 toLight = -normalize(gLightDir.xyz);
    return 0.35f + 0.65f * saturate(dot(n, toLight));
}

// ★ [7-2단계] 마우스 올린 물체를 25% 밝게
float3 ApplyHighlight(float3 color)
{
    return color * (1.0f + 0.25f * gParams.x);
}

float4 PSMain(VSOut pin) : SV_TARGET
{
    return float4(ApplyHighlight(gColor.rgb * Lighting(pin.normalW)), gColor.a);
}

float4 PSBoard(VSOut pin) : SV_TARGET
{
    float4 chalk = gBoardTex.Sample(gSampler, pin.uv);
    float3 color = gColor.rgb * (1.0f - chalk.a) + chalk.rgb;
    color *= Lighting(pin.normalW);

    uint texW, texH;
    gBoardTex.GetDimensions(texW, texH);
    float2 texSize = float2(texW, texH);

    float d     = distance(pin.uv * texSize, gCursor.xy * texSize);
    float edge  = abs(d - gCursor.z);
    float width = fwidth(d) * 1.5f;
    float ring  = (1.0f - smoothstep(width * 0.5f, width, edge)) * gCursor.w;

    color = lerp(color, gCursorColor.rgb, ring);
    return float4(color, 1.0f);
}

// ★ [7-2단계] 버튼: 앞면(-Z)에만 라벨, 나머지 면은 단색
float4 PSLabel(VSOut pin) : SV_TARGET
{
    // 스케일 때문에 노멀 길이가 변해 있으므로 정규화 후 방향 판정
    float3 n = normalize(pin.normalW);

    float3 base = gColor.rgb;
    if (n.z < -0.5f)
    {
        // 면 UV(0~1)를 아틀라스 안의 이 버튼 칸으로 변환
        float2 uv = gUVRect.xy + pin.uv * gUVRect.zw;
        base = gLabelTex.Sample(gSampler, uv).rgb;
    }

    return float4(ApplyHighlight(base * Lighting(pin.normalW)), 1.0f);
}

)HLSL";

// -----------------------------------------------------------------------------
// ★ [4단계] 브러시 셰이더: 도장 하나 = 사각형 하나 (인스턴스)
// 정점 버퍼 없이 SV_VertexID(0~3)로 사각형 꼭짓점을 만들어냄
// -----------------------------------------------------------------------------
inline constexpr char kBrushShaderSource[] = R"HLSL(

struct StampIn
{
    float2 center : CENTER;     // 인스턴스 데이터: 도장 중심 (NDC, -1~1)
    float2 radius : RADIUS;     // 인스턴스 데이터: 반지름 (NDC 단위, x/y 따로)
    float4 color  : COLOR;      // 인스턴스 데이터: 색 (a = 세기)
    uint   vid    : SV_VertexID;// 시스템 값: 이 인스턴스 안에서 몇 번째 정점인지 (0~3)
};

struct StampOut
{
    float4 posH  : SV_POSITION;
    float2 local : LOCAL;       // 도장 중심 기준 좌표 (-1~1) → 원 판정용
    float4 color : COLOR;
};

StampOut VSStamp(StampIn i)
{
    // vid 비트로 꼭짓점 결정 (삼각형 스트립 순서)
    //   0: 좌상(-1, 1)   1: 우상(1, 1)
    //   2: 좌하(-1,-1)   3: 우하(1,-1)
    float2 corner = float2((i.vid & 1) ? 1.0f : -1.0f,
                           (i.vid & 2) ? -1.0f : 1.0f);

    StampOut o;
    o.posH  = float4(i.center + corner * i.radius, 0.0f, 1.0f); // 이미 NDC라 행렬 변환 없음
    o.local = corner;
    o.color = i.color;
    return o;
}

float4 PSStamp(StampOut p) : SV_TARGET
{
    float d = length(p.local);       // 중심에서 거리 (가장자리 = 1)
    if (d > 1.0f) discard;           // 원 밖 픽셀은 버림 → 사각형이 원이 됨

    // 가장자리 25%를 부드럽게 (0.75 → 1.0 사이에서 1 → 0)
    float a = p.color.a * (1.0f - smoothstep(0.75f, 1.0f, d));

    // 프리멀티플라이드 출력: rgb에 알파를 미리 곱함
    return float4(p.color.rgb * a, a);
}

)HLSL";