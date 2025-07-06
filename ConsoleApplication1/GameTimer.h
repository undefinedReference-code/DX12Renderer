#pragma once
#include <Windows.h>

class GameTimer
{
public:
	GameTimer();
	float GameTime()const; // in seconds
	float DeltaTime()const; // in seconds
	void Reset(); // Call before message loop.
	void Start(); // Call when unpaused.
	void Stop(); // Call when paused.
	void Tick(); // Call every frame.
	float TotalTime() const;
private:
	double mSecondsPerCount;
	// Time difference between this frame and the previous. in seconds
	double mDeltaTime;
	// In ticks, the last time point, you call Reset
	// It is regards as the start point of the renderer
	__int64 mBaseTime;
	__int64 mPausedTime;
	__int64 mStopTime;
	__int64 mPrevTime;
	__int64 mCurrTime;
	bool mStopped;
};