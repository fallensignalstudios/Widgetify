#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HttpRouteHandle.h"
#include "HttpResultCallback.h"

class IHttpRouter;
struct FHttpServerRequest;
struct FHttpServerResponse;

/**
 * Local HTTP endpoint for the Figma plugin's "Push to Unreal" button (bound to localhost only).
 *
 *   GET  /figmasync/ping   -> {"ok":true,"project":"...","engine":"5.x"}
 *   POST /figmasync/push   -> body = bundle zip; ?frame=<name>&id=<figma frame id>
 *
 * Pushed bundles are saved to Saved/FigmaSync/Inbox and handed to OnBundleReceived on the next editor tick.
 */
class FFigmaLiveServer : public TSharedFromThis<FFigmaLiveServer>
{
public:
	DECLARE_DELEGATE_TwoParams(FOnBundleReceived, const FString& /*SavedZipPath*/, const FString& /*FrameId*/);

	~FFigmaLiveServer();

	bool Start(uint32 InPort, FString& OutError);
	void Stop();
	bool IsRunning() const { return Router.IsValid(); }
	uint32 GetPort() const { return Port; }

	FOnBundleReceived OnBundleReceived;

private:
	bool HandlePing(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandlePush(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandlePreflight(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool Tick(float DeltaTime);

	static void AddCorsHeaders(FHttpServerResponse& Response);
	static TUniquePtr<FHttpServerResponse> JsonResponse(bool bOk, const FString& Message, int32 StatusCode);

	TSharedPtr<IHttpRouter> Router;
	TArray<FHttpRouteHandle> Routes;
	uint32 Port = 0;
	FTSTicker::FDelegateHandle TickHandle;

	struct FPendingPush
	{
		FString SavedPath;
		FString FrameId;
	};
	TArray<FPendingPush> Pending;
};
