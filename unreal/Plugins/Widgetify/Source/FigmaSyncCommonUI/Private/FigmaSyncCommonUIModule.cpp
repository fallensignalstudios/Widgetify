#include "FigmaCommonUIMapper.h"
#include "FigmaSyncEditorModule.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class FFigmaSyncCommonUIModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		Mapper = MakeShared<FFigmaCommonUIMapper>();
		IFigmaSyncEditorModule::Get().RegisterWidgetMapper(Mapper.ToSharedRef());
	}

	virtual void ShutdownModule() override
	{
		if (Mapper.IsValid() && IFigmaSyncEditorModule::IsAvailable())
		{
			IFigmaSyncEditorModule::Get().UnregisterWidgetMapper(Mapper.ToSharedRef());
		}
		Mapper.Reset();
	}

private:
	TSharedPtr<FFigmaCommonUIMapper> Mapper;
};

IMPLEMENT_MODULE(FFigmaSyncCommonUIModule, FigmaSyncCommonUI);
