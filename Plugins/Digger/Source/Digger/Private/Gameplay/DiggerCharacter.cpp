#include "DiggerCharacter.h"


ADiggerCharacter::ADiggerCharacter()
{
	// Dynamics (Collision switching)
	DiggerDynamics = CreateDefaultSubobject<UDiggerDynamicsComponent>(TEXT("DiggerDynamics"));

	// Smart Light
	DiggerSmartLight = CreateDefaultSubobject<UDiggerLightComponent>(TEXT("DiggerSmartLight"));
    
	// Tool
	DiggerTool = CreateDefaultSubobject<UDiggerToolComponent>(TEXT("DiggerTool"));
}
