//////////////////////////////////////////////////////////////////////////////////////////////////
//The wave parameters are packed into two vec4's like so
// [Length, Speed, Amplitude, Frequency], [Q, reserved for later use, Direction.x, Direction.y]
//////////////////////////////////////////////////////////////////////////////////////////////////
const int MAX_WAVES = 8;
uniform vec4 waveArray[2 * MAX_WAVES];
uniform float elapsedTime;
uniform float WaterHeight;

uniform vec3 cameraRecenter;


float GetHeightOnWaterSuface(vec2 point)
{
   float height = WaterHeight;

   vec2 offsetPos = point.xy - cameraRecenter.xy;
   // There are 2 vec4's of data per wave, so the loop is MAX_WAVES * 2 but increments by 2's
   for(int i = 0; i < MAX_WAVES * 2; i+=2)
   {           
      float speed = waveArray[i].y;
      float freq = waveArray[i].w;
      float amp = waveArray[i].z;
      vec2 waveDir = waveArray[i + 1].zw;
      float k = 1.0 + 2.0 * (waveArray[i+1].x);
      
      float mPlusPhi =  (freq * (speed * elapsedTime + offsetPos.x * waveDir.x + waveDir.y * offsetPos.y)); 
      float sinDir = pow((sin(mPlusPhi) + 1.0) / 2.0, k);
      
      height += amp * sinDir;                    
   }   
   
   return height;
}