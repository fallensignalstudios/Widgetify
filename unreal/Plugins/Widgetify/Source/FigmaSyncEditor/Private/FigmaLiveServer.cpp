#include "FigmaLiveServer.h"
#include "FigmaBundle.h"
#include "FigmaNaming.h"
#include "FigmaSyncCoreModule.h"
#include "FigmaSyncSettings.h"
#include "HAL/FileManager.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Misc/App.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Runtime/Launch/Resources/Version.h"
#include <type_traits>

namespace
{
	/** FHttpRequestHandler is a TFunction up to UE 5.3 and a TDelegate from UE 5.4. */
	template <typename LambdaType>
	FHttpRequestHandler MakeHandler(LambdaType&& Lambda)
	{
#if UE_VERSION_OLDER_THAN(5, 4, 0)
		return FHttpRequestHandler(Forward<LambdaType>(Lambda));
#else
		return FHttpRequestHandler::CreateLambda(Forward<LambdaType>(Lambda));
#endif
	}

	/** The route's verb plus OPTIONS. Older routers key routes by path only, so preflights share the route. */
	EHttpServerRequestVerbs WithPreflight(EHttpServerRequestVerbs Verb)
	{
		using FVerbBits = std::underlying_type_t<EHttpServerRequestVerbs>;
		return static_cast<EHttpServerRequestVerbs>(static_cast<FVerbBits>(Verb) | static_cast<FVerbBits>(EHttpServerRequestVerbs::VERB_OPTIONS));
	}

	FString JsonEscape(const FString& In)
	{
		return In.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\"")).Replace(TEXT("\n"), TEXT("\\n"));
	}
}

FFigmaLiveServer::~FFigmaLiveServer()
{
	Stop();
}

bool FFigmaLiveServer::Start(uint32 InPort, FString& OutError)
{
	Stop();
	Router = FHttpServerModule::Get().GetHttpRouter(InPort, /*bFailOnBindFailure*/ true);
	if (!Router.IsValid())
	{
		OutError = FString::Printf(TEXT("Could not listen on port %u (is another program using it?)."), InPort);
		return false;
	}
	Port = InPort;

	const TWeakPtr<FFigmaLiveServer> WeakThis = AsShared();
	auto Bind = [this, WeakThis](const TCHAR* Path, EHttpServerRequestVerbs Verbs, bool (FFigmaLiveServer::*Method)(const FHttpServerRequest&, const FHttpResultCallback&))
	{
		FHttpRouteHandle Handle = Router->BindRoute(FHttpPath(Path), Verbs, MakeHandler([WeakThis, Method](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			const TSharedPtr<FFigmaLiveServer> Self = WeakThis.Pin();
			return Self.IsValid() ? (Self.Get()->*Method)(Request, OnComplete) : false;
		}));
		if (Handle.IsValid())
		{
			Routes.Add(Handle);
		}
		else
		{
			UE_LOG(LogFigmaSync, Warning, TEXT("Live push: could not bind %s"), Path);
		}
	};
	// Browsers preflight cross-origin requests from the plugin iframe (origin "null") and the server has no CORS of
	// its own, so each route also answers OPTIONS (see HandlePreflight).
	Bind(TEXT("/figmasync/ping"), WithPreflight(EHttpServerRequestVerbs::VERB_GET), &FFigmaLiveServer::HandlePing);
	Bind(TEXT("/figmasync/push"), WithPreflight(EHttpServerRequestVerbs::VERB_POST), &FFigmaLiveServer::HandlePush);

	if (Routes.Num() != 2)
	{
		OutError = TEXT("Could not register the /figmasync routes (already used by another plugin?).");
		Stop();
		return false;
	}
	FHttpServerModule::Get().StartAllListeners();
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this, &FFigmaLiveServer::Tick), 0.2f);
	UE_LOG(LogFigmaSync, Log, TEXT("Widgetify live push listening on http://localhost:%u/figmasync"), Port);
	return true;
}

void FFigmaLiveServer::Stop()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	if (Router.IsValid())
	{
		// Only our routes: StopAllListeners would also stop other plugins' servers (e.g. Remote Control).
		for (const FHttpRouteHandle& Handle : Routes)
		{
			Router->UnbindRoute(Handle);
		}
	}
	Routes.Reset();
	Router.Reset();
	Pending.Reset();
}

void FFigmaLiveServer::AddCorsHeaders(FHttpServerResponse& Response)
{
	Response.Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
	Response.Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("GET, POST, OPTIONS") });
	Response.Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Content-Type") });
	Response.Headers.Add(TEXT("Access-Control-Allow-Private-Network"), { TEXT("true") });
}

TUniquePtr<FHttpServerResponse> FFigmaLiveServer::JsonResponse(bool bOk, const FString& Message, int32 StatusCode)
{
	const FString Body = FString::Printf(TEXT("{\"ok\":%s,\"message\":\"%s\"}"), bOk ? TEXT("true") : TEXT("false"), *JsonEscape(Message));
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Body, FString(TEXT("application/json")));
	Response->Code = static_cast<EHttpServerResponseCodes>(StatusCode);
	AddCorsHeaders(*Response);
	return Response;
}

bool FFigmaLiveServer::HandlePing(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	if (Request.Verb == EHttpServerRequestVerbs::VERB_OPTIONS)
	{
		return HandlePreflight(Request, OnComplete);
	}
	const FString Body = FString::Printf(TEXT("{\"ok\":true,\"name\":\"Widgetify\",\"version\":\"0.1.0\",\"project\":\"%s\",\"engine\":\"%d.%d\"}"),
		*JsonEscape(FApp::GetProjectName()), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION);
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Body, FString(TEXT("application/json")));
	AddCorsHeaders(*Response);
	OnComplete(MoveTemp(Response));
	return true;
}

bool FFigmaLiveServer::HandlePreflight(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Ok();
	AddCorsHeaders(*Response);
	OnComplete(MoveTemp(Response));
	return true;
}

bool FFigmaLiveServer::HandlePush(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	if (Request.Verb == EHttpServerRequestVerbs::VERB_OPTIONS)
	{
		return HandlePreflight(Request, OnComplete);
	}
	if (Request.Body.Num() == 0)
	{
		OnComplete(JsonResponse(false, TEXT("Empty request: send the bundle zip as the body."), 400));
		return true;
	}

	// Validate before saving so a bad push never replaces a good bundle on disk.
	FString Error;
	TArray<uint8> Copy = Request.Body;
	const TSharedPtr<FFigmaBundle> Pushed = FFigmaBundle::OpenFromZipBytes(MoveTemp(Copy), TEXT("Live push"), Error);
	if (!Pushed.IsValid())
	{
		OnComplete(JsonResponse(false, FString::Printf(TEXT("Not a valid bundle: %s"), *Error), 400));
		return true;
	}
	// The frame comes from the bundle itself: query parameters may arrive percent-encoded (Figma ids contain ':').
	const FFigmaDocument& PushedDocument = Pushed->GetDocument();
	const FString FrameName = PushedDocument.FrameName.IsEmpty() ? FString(TEXT("Pushed")) : PushedDocument.FrameName;
	const FString FrameId = PushedDocument.FrameId;

	const FString InboxDir = FPaths::ProjectSavedDir() / TEXT("FigmaSync") / TEXT("Inbox");
	IFileManager::Get().MakeDirectory(*InboxDir, true);
	const FString SavedPath = FPaths::ConvertRelativePathToFull(InboxDir / (FFigmaNaming::SanitizeIdentifier(FrameName, TEXT("Pushed")) + TEXT(".figmasync.zip")));
	if (!FFileHelper::SaveArrayToFile(Request.Body, *SavedPath))
	{
		OnComplete(JsonResponse(false, FString::Printf(TEXT("Could not write %s"), *SavedPath), 500));
		return true;
	}

	FString Message;
	const FSoftObjectPath* Bound = FrameId.IsEmpty() ? nullptr : UFigmaSyncUserState::Get().FrameBindings.Find(FrameId);
	if (Bound && UFigmaSyncSettings::Get().bAutoApplyPushes)
	{
		Message = FString::Printf(TEXT("Received '%s'; syncing %s."), *FrameName, *Bound->GetAssetName());
	}
	else
	{
		Message = FString::Printf(TEXT("Received '%s'; review it in the Figma Sync panel."), *FrameName);
	}
	Pending.Add({ SavedPath, FrameId });
	OnComplete(JsonResponse(true, Message, 200));
	return true;
}

bool FFigmaLiveServer::Tick(float DeltaTime)
{
	if (Pending.Num() > 0)
	{
		// Import outside the HTTP callback, one push per tick.
		const FPendingPush Push = Pending[0];
		Pending.RemoveAt(0);
		OnBundleReceived.ExecuteIfBound(Push.SavedPath, Push.FrameId);
	}
	return true;
}
