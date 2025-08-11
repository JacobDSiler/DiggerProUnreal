@ECHO OFF
SET DXC="C:\UE_5.2\Engine\Binaries\ThirdParty\ShaderConductor\Win64\dxc.exe"
IF NOT EXIST %DXC% (
	ECHO Couldn't find dxc.exe under "C:\UE_5.2\Engine\Binaries\ThirdParty\ShaderConductor\Win64"
	GOTO :END
)
%DXC% /auto-binding-space 0 /Zpr /O3 -Wno-parentheses-equality -disable-lifetime-markers /T ps_6_6 /E MainPS /Fc VirtualTextureMaterial.d3dasm /Fo VirtualTextureMaterial.dxil VirtualTextureMaterial.usf
:END
PAUSE
