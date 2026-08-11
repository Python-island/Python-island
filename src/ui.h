#pragma once

// Draw DynamicIsland main UI
void DrawIslandUI(bool isMouseOver, bool isFullscreen, float animationY, float deltaTime);

// Draw settings window
void DrawSettingsWindow();

// Check if mouse is over island (Windows only — Linux uses GLFW_HOVERED)
#ifdef _WIN32
bool IsMouseOverIsland();
#endif
