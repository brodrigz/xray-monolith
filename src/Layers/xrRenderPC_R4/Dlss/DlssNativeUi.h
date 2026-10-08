#pragma once
#include <algorithm>

namespace dlss
{
// The queues retain the original draw packets (including skinning and object
// transforms) until the display-resolution pass in the SAME rendered view.
template<class Queue, class Predicate>
void ExtractNativeUi(Queue& scene, Queue& display, Predicate isUi)
{
    display.clear();
    scene.erase(std::remove_if(scene.begin(), scene.end(), [&](const auto& item)
    {
        if (!isUi(item)) return false;
        display.push_back(item);
        return true;
    }), scene.end());
}

template<class Queue, class DisplayQueue>
void RemoveNativeUiDuplicates(Queue& scene, const DisplayQueue& display)
{
    scene.erase(std::remove_if(scene.begin(), scene.end(), [&](const auto& item)
    {
        return std::any_of(display.begin(), display.end(), [&](const auto& ui)
        {
            return item.pVisual == ui.pVisual && item.pMatrix == ui.pMatrix;
        });
    }), scene.end());
}
}
