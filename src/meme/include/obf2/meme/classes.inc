// Класи `dice::meme::*` та їхні поля, у порядку читання.
//
// Створено `tools/meme_read.py --cpp`. **Руками не правити.**
//
// Джерело — `MemeDll.dll` і `MemeBf.dll` із теки мода: вони
// експортують повні символи C++, і кожен `onStream` передає назву
// поля рядком, а тип поля — це те, який метод потоку викликано.
// Успадковані поля вже розгорнуті на місці.

MEME_CLASS(Action)
MEME_CLASS_END()

MEME_CLASS(ActionFunction)
  MEME_FIELD("Action", Object)
MEME_CLASS_END()

MEME_CLASS(ActionListAction)
  MEME_FIELD("Action list", List)
MEME_CLASS_END()

MEME_CLASS(ActionNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Action", Object)
MEME_CLASS_END()

MEME_CLASS(AddData)
  MEME_FIELD("Data 1", Object)
  MEME_FIELD("Data 2", Object)
MEME_CLASS_END()

MEME_CLASS(AlphaFadeEffect)
MEME_CLASS_END()

MEME_CLASS(AndData)
  MEME_FIELD("Data 1", Object)
  MEME_FIELD("Data 2", Object)
MEME_CLASS_END()

MEME_CLASS(AnyKeyEvent)
  MEME_FIELD("Event type", Index)
MEME_CLASS_END()

MEME_CLASS(BfAddSubEffectNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Value", Object)
  MEME_FIELD("End value", Object)
  MEME_FIELD("Start time", Object)
  MEME_FIELD("Start percentage", Object)
  MEME_FIELD("End time", Object)
  MEME_FIELD("Delay", Object)
  MEME_FIELD("Up", Object)
  MEME_FIELD("Go", Object)
MEME_CLASS_END()

MEME_CLASS(BfAddSubNextEffectNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Value", Object)
  MEME_FIELD("End value", Object)
  MEME_FIELD("Start time", Object)
  MEME_FIELD("Start percentage", Object)
  MEME_FIELD("End time", Object)
  MEME_FIELD("Delay", Object)
  MEME_FIELD("Up", Object)
  MEME_FIELD("Next action", Object)
  MEME_FIELD("Go", Object)
MEME_CLASS_END()

MEME_CLASS(BfAnimationNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Frames", Int)
  MEME_FIELD("Start", Bool)
  MEME_FIELD("Looping", Bool)
MEME_CLASS_END()

MEME_CLASS(BfBinkNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Background picture", Object)
  MEME_FIELD("Fade time", Float)
  MEME_FIELD("Play", Object)
MEME_CLASS_END()

MEME_CLASS(BfButtonNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Picture", Name)
  MEME_FIELD("Mouse over picture", Name)
  MEME_FIELD("Action", Object)
  MEME_FIELD("Width", Float)
  MEME_FIELD("Height", Float)
MEME_CLASS_END()

MEME_CLASS(BfCenterStyle)
MEME_CLASS_END()

MEME_CLASS(BfColorData)
  MEME_FIELD("Red", Float)
  MEME_FIELD("Green", Float)
  MEME_FIELD("Blue", Float)
  MEME_FIELD("Alpha", Float)
MEME_CLASS_END()

MEME_CLASS(BfColorFillNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Picture", Name)
  MEME_FIELD("Data", Object)
  MEME_FIELD("Maximum value", Object)
  MEME_FIELD("Horizontal align", Bool)
  MEME_FIELD("Fill order", Bool)
  MEME_FIELD("Color", Object)
  MEME_FIELD("Fill color", Object)
MEME_CLASS_END()

MEME_CLASS(BfCreditsNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Value", Object)
  MEME_FIELD("End value", Float)
  MEME_FIELD("Reset value", Float)
  MEME_FIELD("Time", Float)
  MEME_FIELD("Go", Object)
MEME_CLASS_END()

MEME_CLASS(BfCrosshairNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Radius", Object)
  MEME_FIELD("Thickness", Object)
  MEME_FIELD("outline thickness", Object)
  MEME_FIELD("Deviation", Object)
  MEME_FIELD("color", Object)
  MEME_FIELD("outline", Object)
MEME_CLASS_END()

MEME_CLASS(BfEditData)
MEME_CLASS_END()

MEME_CLASS(BfEditNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Font", Name)
  MEME_FIELD("String", Object)
  MEME_FIELD("Select action", Object)
  MEME_FIELD("Editbox data", Object)
  MEME_FIELD("Focus", Bool)
MEME_CLASS_END()

MEME_CLASS(BfEditNodeInt)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Font", Name)
  MEME_FIELD("Int", Object)
  MEME_FIELD("String", Object)
  MEME_FIELD("Select action", Object)
  MEME_FIELD("Editbox data", Object)
  MEME_FIELD("Focus", Bool)
MEME_CLASS_END()

MEME_CLASS(BfEditNodeNew)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Font", Name)
  MEME_FIELD("String", Object)
  MEME_FIELD("Index", Int)
  MEME_FIELD("Max index", Int)
  MEME_FIELD("Current index", Object)
MEME_CLASS_END()

MEME_CLASS(BfFixedSliderNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Cursor node", Object)
  MEME_FIELD("Data", Object)
  MEME_FIELD("Minimum value", Float)
  MEME_FIELD("Maximum value", Float)
  MEME_FIELD("Number visible", Float)
  MEME_FIELD("Interval", Float)
MEME_CLASS_END()

MEME_CLASS(BfGaugeNode)
  MEME_FIELD("color", Object)
  MEME_FIELD("background color", Object)
  MEME_FIELD("Data", Object)
  MEME_FIELD("Maximum value", Object)
  MEME_FIELD("Horizontal align", Bool)
MEME_CLASS_END()

MEME_CLASS(BfLeftOutlineStyle)
MEME_CLASS_END()

MEME_CLASS(BfListBoxData)
  MEME_FIELD("Create new action", Object)
  MEME_FIELD("Create new string", Object)
  MEME_FIELD("Profile name", Object)
MEME_CLASS_END()

MEME_CLASS(BfListBoxNode)
MEME_CLASS_END()

MEME_CLASS(BfLocaleData)
  MEME_FIELD("Valid", Object)
MEME_CLASS_END()

MEME_CLASS(BfLocaleNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Locale", Object)
MEME_CLASS_END()

MEME_CLASS(BfLocaleStringData)
  MEME_FIELD("Locale", Object)
  MEME_FIELD("Valid", Object)
  MEME_FIELD("String Id", Object)
MEME_CLASS_END()

MEME_CLASS(BfMultiplyColorEffect)
  MEME_FIELD("Red", Object)
  MEME_FIELD("Green", Object)
  MEME_FIELD("Blue", Object)
  MEME_FIELD("Alpha", Object)
MEME_CLASS_END()

MEME_CLASS(BfMultiplyColorEffect2)
  MEME_FIELD("Alpha", Object)
MEME_CLASS_END()

MEME_CLASS(BfNavigationButtonNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Picture", Name)
  MEME_FIELD("Mouse over picture", Name)
  MEME_FIELD("Clicked picture", Name)
  MEME_FIELD("Action", Object)
  MEME_FIELD("MouseOver button", Bool)
  MEME_FIELD("Index", Int)
  MEME_FIELD("Current mouseover index", Object)
  MEME_FIELD("Current clicked index", Object)
  MEME_FIELD("MouseOver active", Object)
  MEME_FIELD("Width", Float)
  MEME_FIELD("Height", Float)
MEME_CLASS_END()

MEME_CLASS(BfNewListBoxNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Listbox data", Object)
  MEME_FIELD("Font", Name)
  MEME_FIELD("Select action", Object)
  MEME_FIELD("Focus action", Object)
  MEME_FIELD("Step sound", Name)
  MEME_FIELD("Select sound", Name)
  MEME_FIELD("Failed select sound", Name)
  MEME_FIELD("Row height", Float)
  MEME_FIELD("IsSelectable", Bool)
  MEME_FIELD("Border or not", Bool)
  MEME_FIELD("Background color red", Float)
  MEME_FIELD("Background color green", Float)
  MEME_FIELD("Background color blue", Float)
  MEME_FIELD("Background color alpha", Float)
  MEME_FIELD("Scrollbar width", Float)
  MEME_FIELD("Frame color red", Float)
  MEME_FIELD("Frame color green", Float)
  MEME_FIELD("Frame color blue", Float)
  MEME_FIELD("Frame color alpha", Float)
  MEME_FIELD("Select color red", Float)
  MEME_FIELD("Select color green", Float)
  MEME_FIELD("Select color blue", Float)
  MEME_FIELD("Select color alpha", Float)
  MEME_FIELD("Show tooltip", Bool)
  MEME_FIELD("Scrollbar offset from border", Float)
  MEME_FIELD("Outlands_2.dif", Object)
  MEME_FIELD("Outlands_2_inv.dif", Object)
MEME_CLASS_END()

MEME_CLASS(BfOccupiedVehicleData)
MEME_CLASS_END()

MEME_CLASS(BfOccupiedVehicleNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Position", Int)
  MEME_FIELD("Draw debug pic", Bool)
  MEME_FIELD("BfOccupiedVehicleData", Object)
MEME_CLASS_END()

MEME_CLASS(BfOutlineStyle)
MEME_CLASS_END()

MEME_CLASS(BfPictureFillNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Picture", Name)
  MEME_FIELD("Fill picture", Name)
  MEME_FIELD("Data", Object)
  MEME_FIELD("Maximum value", Object)
  MEME_FIELD("Horizontal align", Bool)
  MEME_FIELD("Fill order", Bool)
MEME_CLASS_END()

MEME_CLASS(BfPlayerInfoData)
MEME_CLASS_END()

MEME_CLASS(BfPlayerInfoNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("PlayerInfo", Object)
MEME_CLASS_END()

MEME_CLASS(BfPlayerInfoStringData)
  MEME_FIELD("Row", Int)
  MEME_FIELD("Col", Int)
  MEME_FIELD("PlayerInfo", Object)
MEME_CLASS_END()

MEME_CLASS(BfPlayerInfoStringTeamData)
  MEME_FIELD("Row", Int)
  MEME_FIELD("Col", Int)
  MEME_FIELD("Team", Int)
  MEME_FIELD("PlayerInfo", Object)
MEME_CLASS_END()

MEME_CLASS(BfRectangle)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Thickness", Float)
MEME_CLASS_END()

MEME_CLASS(BfScrollPictureNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Scroll picture", Name)
  MEME_FIELD("Data", Object)
  MEME_FIELD("Maximum value", Object)
  MEME_FIELD("Y offset", Int)
  MEME_FIELD("Size", Int)
  MEME_FIELD("Var size", Object)
  MEME_FIELD("Maintain value", Object)
  MEME_FIELD("From bottom", Bool)
MEME_CLASS_END()

MEME_CLASS(BfSelectButtonNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Picture", Name)
  MEME_FIELD("Mouse over picture", Name)
  MEME_FIELD("Clicked picture", Name)
  MEME_FIELD("Action", Object)
  MEME_FIELD("Index", Int)
  MEME_FIELD("Current clicked index", Object)
  MEME_FIELD("Width", Float)
  MEME_FIELD("Height", Float)
MEME_CLASS_END()

MEME_CLASS(BfSliderNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Cursor node", Object)
  MEME_FIELD("Data", Object)
  MEME_FIELD("Minimum value", Float)
  MEME_FIELD("Maximum value", Float)
  MEME_FIELD("Number visible", Float)
MEME_CLASS_END()

MEME_CLASS(BfStyle)
MEME_CLASS_END()

MEME_CLASS(BfStyle2)
MEME_CLASS_END()

MEME_CLASS(BfTextNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("String", Object)
  MEME_FIELD("Style", Object)
  MEME_FIELD("Type", Object)
MEME_CLASS_END()

MEME_CLASS(BfTransformNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("X", Object)
  MEME_FIELD("Y", Object)
  MEME_FIELD("Width", Float)
  MEME_FIELD("Height", Float)
  MEME_FIELD("Transformed node", Object)
MEME_CLASS_END()

MEME_CLASS(BfTransformNodeSize)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("X", Float)
  MEME_FIELD("Y", Float)
  MEME_FIELD("Width", Object)
  MEME_FIELD("Height", Object)
  MEME_FIELD("Transformed node", Object)
MEME_CLASS_END()

MEME_CLASS(BfVariablePictureFillNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Picture", Name)
  MEME_FIELD("Fill picture", Name)
  MEME_FIELD("Data", Object)
  MEME_FIELD("Maximum value", Object)
  MEME_FIELD("Size", Int)
  MEME_FIELD("Horizontal align", Bool)
  MEME_FIELD("Fill order", Bool)
MEME_CLASS_END()

MEME_CLASS(BfVariablePictureFillNode2)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Picture", Object)
  MEME_FIELD("Fill picture", Object)
  MEME_FIELD("Data", Object)
  MEME_FIELD("Maximum value", Object)
  MEME_FIELD("Size", Object)
  MEME_FIELD("Horizontal align", Bool)
  MEME_FIELD("Fill order", Bool)
MEME_CLASS_END()

MEME_CLASS(BfVariablePictureNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("picture str", Object)
  MEME_FIELD("redraw", Object)
MEME_CLASS_END()

MEME_CLASS(BfVariableTimeoutActionNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Action", Object)
  MEME_FIELD("Timeout time", Object)
MEME_CLASS_END()

MEME_CLASS(BfVariableTimeoutActionNode2)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Action", Object)
  MEME_FIELD("Current time", Object)
  MEME_FIELD("Timeout time", Object)
MEME_CLASS_END()

MEME_CLASS(BfVerticalScrollExNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Version <do not edit>", Int)
  MEME_FIELD("Text", Object)
  MEME_FIELD("Style", Object)
  MEME_FIELD("Scroll speed", Float)
  MEME_FIELD("Scroll speed multiplier", Object)
  MEME_FIELD("Restart toggle", Object)
MEME_CLASS_END()

MEME_CLASS(BfVerticalScrollNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Text", Object)
  MEME_FIELD("Font", Name)
  MEME_FIELD("Scroll speed", Float)
  MEME_FIELD("Scroll speed multiplier", Object)
  MEME_FIELD("Restart toggle", Object)
MEME_CLASS_END()

MEME_CLASS(BlendFuncEffect)
  MEME_FIELD("Source blend func", Index)
  MEME_FIELD("Destination blend func", Index)
MEME_CLASS_END()

MEME_CLASS(BoolData)
  MEME_FIELD("Value <do not edit>", Bool)
MEME_CLASS_END()

MEME_CLASS(BoolRefData)
MEME_CLASS_END()

MEME_CLASS(Button)
MEME_CLASS_END()

MEME_CLASS(ButtonEvent)
  MEME_FIELD("Event type", Index)
  MEME_FIELD("Button type", Index)
MEME_CLASS_END()

MEME_CLASS(ButtonPressIterator)
MEME_CLASS_END()

MEME_CLASS(ButtonReleaseIterator)
MEME_CLASS_END()

MEME_CLASS(CachedIStream)
MEME_CLASS_END()

MEME_CLASS(CallFunctionAction)
  MEME_FIELD("Function", Object)
MEME_CLASS_END()

MEME_CLASS(CallFunctionVariableAction)
  MEME_FIELD("Function", Object)
  MEME_FIELD("Result data", Object)
MEME_CLASS_END()

MEME_CLASS(CenterAlignedScaleYStyle)
  MEME_FIELD("Font handle", Name)
MEME_CLASS_END()

MEME_CLASS(CenterAlignedStyle)
  MEME_FIELD("Font handle", Name)
MEME_CLASS_END()

MEME_CLASS(ClassIStream)
MEME_CLASS_END()

MEME_CLASS(ClassOStream)
MEME_CLASS_END()

MEME_CLASS(ClearStream)
MEME_CLASS_END()

MEME_CLASS(ClipEffect)
  MEME_FIELD("X", Float)
  MEME_FIELD("Y", Float)
  MEME_FIELD("Width", Float)
  MEME_FIELD("Height", Float)
MEME_CLASS_END()

MEME_CLASS(ClipNode)
MEME_CLASS_END()

MEME_CLASS(Color)
MEME_CLASS_END()

MEME_CLASS(ColorEffect)
  MEME_FIELD("Red", Float)
  MEME_FIELD("Green", Float)
  MEME_FIELD("Blue", Float)
  MEME_FIELD("Alpha", Float)
MEME_CLASS_END()

MEME_CLASS(ConcatenateListData)
  MEME_FIELD("Data list", List)
MEME_CLASS_END()

MEME_CLASS(Coordinate2)
MEME_CLASS_END()

MEME_CLASS(CullActionNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Action", Object)
MEME_CLASS_END()

MEME_CLASS(CullEventActionNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Action", Object)
  MEME_FIELD("Event", Object)
MEME_CLASS_END()

MEME_CLASS(CullEventTimeoutActionNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Action", Object)
  MEME_FIELD("Event", Object)
  MEME_FIELD("Timeout time", Float)
MEME_CLASS_END()

MEME_CLASS(CullNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Data", Object)
  MEME_FIELD("In time", Float)
  MEME_FIELD("Out time", Float)
MEME_CLASS_END()

MEME_CLASS(CullVariableActionNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Action", Object)
  MEME_FIELD("Variable", Object)
MEME_CLASS_END()

MEME_CLASS(CullVariableAndEventActionNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Action", Object)
  MEME_FIELD("Variable", Object)
  MEME_FIELD("Event", Object)
MEME_CLASS_END()

MEME_CLASS(CullVariableNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Variable", Object)
  MEME_FIELD("Data", Object)
MEME_CLASS_END()

MEME_CLASS(CurserNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Width", Object)
  MEME_FIELD("Height", Object)
  MEME_FIELD("X", Object)
  MEME_FIELD("Y", Object)
MEME_CLASS_END()

MEME_CLASS(Data)
MEME_CLASS_END()

MEME_CLASS(DataListData)
  MEME_FIELD("Data list", List)
MEME_CLASS_END()

MEME_CLASS(DeselectIterator)
MEME_CLASS_END()

MEME_CLASS(DirectoryLink)
MEME_CLASS_END()

MEME_CLASS(DisableNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Disable data", Object)
MEME_CLASS_END()

MEME_CLASS(DivData)
  MEME_FIELD("Data 1", Object)
  MEME_FIELD("Data 2", Object)
MEME_CLASS_END()

MEME_CLASS(DragWindowNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("X", Float)
  MEME_FIELD("Y", Float)
  MEME_FIELD("Width", Float)
  MEME_FIELD("Height", Float)
  MEME_FIELD("Transformed node", Object)
MEME_CLASS_END()

MEME_CLASS(DynamicIndexPointer)
MEME_CLASS_END()

MEME_CLASS(EditNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Font", Name)
  MEME_FIELD("String", Object)
MEME_CLASS_END()

MEME_CLASS(Effect)
MEME_CLASS_END()

MEME_CLASS(EffectNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Effect", Object)
MEME_CLASS_END()

MEME_CLASS(EffectPipe)
MEME_CLASS_END()

MEME_CLASS(EqualData)
  MEME_FIELD("Data 1", Object)
  MEME_FIELD("Data 2", Object)
MEME_CLASS_END()

MEME_CLASS(Event)
MEME_CLASS_END()

MEME_CLASS(EventIterator)
MEME_CLASS_END()

MEME_CLASS(ExclusiveOrData)
  MEME_FIELD("Data 1", Object)
  MEME_FIELD("Data 2", Object)
MEME_CLASS_END()

MEME_CLASS(ExtendedButtonEvent)
  MEME_FIELD("Event type", Index)
  MEME_FIELD("Button type", Index)
  MEME_FIELD("Repeat count", Int)
MEME_CLASS_END()

MEME_CLASS(ExtendedFocusToPressedEffectNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Effect", Object)
  MEME_FIELD("Min value", Float)
  MEME_FIELD("Max value", Float)
MEME_CLASS_END()

MEME_CLASS(ExtendedNormalToFocusEffectNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Effect", Object)
  MEME_FIELD("Min value", Float)
  MEME_FIELD("Max value", Float)
MEME_CLASS_END()

MEME_CLASS(ExtendedRememberingTabstopManagerNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Index", Object)
MEME_CLASS_END()

MEME_CLASS(ExtendedUnicodeEvent)
  MEME_FIELD("Event type", Index)
  MEME_FIELD("Unicode", Wchar)
  MEME_FIELD("Repeat count", Int)
MEME_CLASS_END()

MEME_CLASS(FindTabstopCoordinateIndexIterator)
MEME_CLASS_END()

MEME_CLASS(FindTabstopIndexCoordinateIterator)
MEME_CLASS_END()

MEME_CLASS(FloatArrayData)
MEME_CLASS_END()

MEME_CLASS(FloatData)
  MEME_FIELD("Value <do not edit>", Float)
MEME_CLASS_END()

MEME_CLASS(FloatRefData)
MEME_CLASS_END()

MEME_CLASS(FocusEffectNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Effect", Object)
MEME_CLASS_END()

MEME_CLASS(FocusLockedData)
MEME_CLASS_END()

MEME_CLASS(FocusNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Focus X", Object)
  MEME_FIELD("Focus Y", Object)
MEME_CLASS_END()

MEME_CLASS(FocusToPressedEffectNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Effect", Object)
MEME_CLASS_END()

MEME_CLASS(Function)
MEME_CLASS_END()

MEME_CLASS(IStream)
  MEME_FIELD("Int", Int)
  MEME_FIELD("Int", Int)
MEME_CLASS_END()

MEME_CLASS(IfElseEventActionNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Action", Object)
  MEME_FIELD("Variable", Object)
  MEME_FIELD("Else Action", Object)
  MEME_FIELD("Event", Object)
MEME_CLASS_END()

MEME_CLASS(IndexCountData)
  MEME_FIELD("Data", Object)
MEME_CLASS_END()

MEME_CLASS(IndexDataData)
  MEME_FIELD("Data", Object)
  MEME_FIELD("Index", Object)
MEME_CLASS_END()

MEME_CLASS(IntData)
  MEME_FIELD("Value <do not edit>", Int)
MEME_CLASS_END()

MEME_CLASS(IntRefData)
MEME_CLASS_END()

MEME_CLASS(Iterator)
MEME_CLASS_END()

MEME_CLASS(LastInputIndexAction)
  MEME_FIELD("Last input index", Object)
MEME_CLASS_END()

MEME_CLASS(LessData)
  MEME_FIELD("Data 1", Object)
  MEME_FIELD("Data 2", Object)
MEME_CLASS_END()

MEME_CLASS(LessEqualData)
  MEME_FIELD("Data 1", Object)
  MEME_FIELD("Data 2", Object)
MEME_CLASS_END()

MEME_CLASS(List)
MEME_CLASS_END()

MEME_CLASS(ListBoxData)
MEME_CLASS_END()

MEME_CLASS(LockFocusAction)
MEME_CLASS_END()

MEME_CLASS(LockPointerAction)
  MEME_FIELD("Input index data", Object)
MEME_CLASS_END()

MEME_CLASS(LogicalData)
  MEME_FIELD("Data 1", Object)
  MEME_FIELD("Data 2", Object)
MEME_CLASS_END()

MEME_CLASS(Memory)
MEME_CLASS_END()

MEME_CLASS(MemoryOStream)
  MEME_FIELD("Int", Int)
  MEME_FIELD("Int", Int)
MEME_CLASS_END()

MEME_CLASS(MessageManager)
MEME_CLASS_END()

MEME_CLASS(Mode)
MEME_CLASS_END()

MEME_CLASS(ModifyVariableAction)
  MEME_FIELD("Variable", Object)
  MEME_FIELD("Modify value", Float)
  MEME_FIELD("Limit value", Float)
MEME_CLASS_END()

MEME_CLASS(MoveClipEffect)
  MEME_FIELD("X", Float)
  MEME_FIELD("Y", Float)
  MEME_FIELD("Width", Float)
  MEME_FIELD("Height", Float)
  MEME_FIELD("Move length", Float)
  MEME_FIELD("Move direction", Float)
MEME_CLASS_END()

MEME_CLASS(MoveEffect)
  MEME_FIELD("Move length", Object)
  MEME_FIELD("Move direction", Object)
MEME_CLASS_END()

MEME_CLASS(MoveNoClipEffect)
  MEME_FIELD("Move length", Object)
  MEME_FIELD("Move direction", Object)
MEME_CLASS_END()

MEME_CLASS(MovedOutIterator)
MEME_CLASS_END()

MEME_CLASS(MovingOutIterator)
MEME_CLASS_END()

MEME_CLASS(MulData)
  MEME_FIELD("Data 1", Object)
  MEME_FIELD("Data 2", Object)
MEME_CLASS_END()

MEME_CLASS(MultiplyColorEffect)
  MEME_FIELD("Red", Float)
  MEME_FIELD("Green", Float)
  MEME_FIELD("Blue", Float)
  MEME_FIELD("Alpha", Float)
MEME_CLASS_END()

MEME_CLASS(MultiplyVertexColorEffect)
MEME_CLASS_END()

MEME_CLASS(NameLink)
MEME_CLASS_END()

MEME_CLASS(NameNode)
  MEME_FIELD("Next node", Object)
MEME_CLASS_END()

MEME_CLASS(NameTree)
MEME_CLASS_END()

MEME_CLASS(NavigateAction)
  MEME_FIELD("Action tree", Object)
MEME_CLASS_END()

MEME_CLASS(NavigateChildAction)
MEME_CLASS_END()

MEME_CLASS(NavigateChildAndRunAction)
MEME_CLASS_END()

MEME_CLASS(NavigateFirstSiblingAction)
MEME_CLASS_END()

MEME_CLASS(NavigateLastSiblingAction)
MEME_CLASS_END()

MEME_CLASS(NavigateNextSiblingAction)
MEME_CLASS_END()

MEME_CLASS(NavigateParentAction)
MEME_CLASS_END()

MEME_CLASS(NavigateParentAndRunAction)
MEME_CLASS_END()

MEME_CLASS(NavigatePreviousSiblingAction)
MEME_CLASS_END()

MEME_CLASS(NavigateRunAction)
MEME_CLASS_END()

MEME_CLASS(NavigateRunAndChildAction)
MEME_CLASS_END()

MEME_CLASS(NavigateSiblingIndexAction)
  MEME_FIELD("Action tree", Object)
  MEME_FIELD("Index", Int)
MEME_CLASS_END()

MEME_CLASS(NavigateTree)
MEME_CLASS_END()

MEME_CLASS(NavigateTreeObject)
  MEME_FIELD("Description", Object)
  MEME_FIELD("Action", Object)
MEME_CLASS_END()

MEME_CLASS(NavigateViewNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Navigate tree", Object)
  MEME_FIELD("Font", Name)
MEME_CLASS_END()

MEME_CLASS(Node)
  MEME_FIELD("Next node", Object)
MEME_CLASS_END()

MEME_CLASS(NormalToFocusEffectNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Effect", Object)
MEME_CLASS_END()

MEME_CLASS(NotData)
  MEME_FIELD("Data", Object)
MEME_CLASS_END()

MEME_CLASS(NotEqualData)
  MEME_FIELD("Data 1", Object)
  MEME_FIELD("Data 2", Object)
MEME_CLASS_END()

MEME_CLASS(NotFocusEffectNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Effect", Object)
MEME_CLASS_END()

MEME_CLASS(OStream)
MEME_CLASS_END()

MEME_CLASS(Object)
MEME_CLASS_END()

MEME_CLASS(ObjectLink)
  MEME_FIELD("Object", Bool)
MEME_CLASS_END()

MEME_CLASS(ObjectList)
MEME_CLASS_END()

MEME_CLASS(OperatorData)
  MEME_FIELD("Data 1", Object)
  MEME_FIELD("Data 2", Object)
MEME_CLASS_END()

MEME_CLASS(OrData)
  MEME_FIELD("Data 1", Object)
  MEME_FIELD("Data 2", Object)
MEME_CLASS_END()

MEME_CLASS(OutputIStream)
MEME_CLASS_END()

MEME_CLASS(PaintData)
MEME_CLASS_END()

MEME_CLASS(PathNode)
  MEME_FIELD("Next node", Object)
MEME_CLASS_END()

MEME_CLASS(PictureNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Picture", Name)
MEME_CLASS_END()

MEME_CLASS(PictureUVNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Picture", Name)
  MEME_FIELD("U", Float)
  MEME_FIELD("V", Float)
  MEME_FIELD("Width", Float)
  MEME_FIELD("Height", Float)
MEME_CLASS_END()

MEME_CLASS(PlayerInputIndexData)
MEME_CLASS_END()

MEME_CLASS(Pointer)
MEME_CLASS_END()

MEME_CLASS(PointerXData)
MEME_CLASS_END()

MEME_CLASS(PointerYData)
MEME_CLASS_END()

MEME_CLASS(PopPathAction)
  MEME_FIELD("Stack path node", Object)
  MEME_FIELD("In time", Float)
  MEME_FIELD("Out time", Float)
  MEME_FIELD("In wait time", Float)
  MEME_FIELD("Out wait time", Float)
  MEME_FIELD("Paint outnode over innode", Bool)
MEME_CLASS_END()

MEME_CLASS(ProxyIStream)
MEME_CLASS_END()

MEME_CLASS(ProxyOStream)
MEME_CLASS_END()

MEME_CLASS(PushPathAction)
  MEME_FIELD("Stack path node", Object)
  MEME_FIELD("Destination node", Object)
  MEME_FIELD("In time", Float)
  MEME_FIELD("Out time", Float)
  MEME_FIELD("In wait time", Float)
  MEME_FIELD("Out wait time", Float)
  MEME_FIELD("Paint outnode over innode", Bool)
MEME_CLASS_END()

MEME_CLASS(Rectangle)
MEME_CLASS_END()

MEME_CLASS(RegisterStream)
MEME_CLASS_END()

MEME_CLASS(RememberingTabstopManagerNode)
  MEME_FIELD("Next node", Object)
MEME_CLASS_END()

MEME_CLASS(RemoveEventAction)
MEME_CLASS_END()

MEME_CLASS(RightAlignedStyle)
  MEME_FIELD("Font handle", Name)
MEME_CLASS_END()

MEME_CLASS(RotateAroundCoordinateEffect)
  MEME_FIELD("Start angle", Object)
  MEME_FIELD("Angle multiplyer", Object)
  MEME_FIELD("X", Object)
  MEME_FIELD("Y", Object)
MEME_CLASS_END()

MEME_CLASS(RotateEffect)
  MEME_FIELD("Start angle", Object)
  MEME_FIELD("Angle multiplyer", Object)
MEME_CLASS_END()

MEME_CLASS(RowDistanceStyle)
  MEME_FIELD("Font handle", Name)
  MEME_FIELD("Row distance", Float)
MEME_CLASS_END()

MEME_CLASS(RuntimeClass)
MEME_CLASS_END()

MEME_CLASS(RuntimeObject)
MEME_CLASS_END()

MEME_CLASS(ScaleYStyle)
  MEME_FIELD("Font handle", Name)
MEME_CLASS_END()

MEME_CLASS(SelectIterator)
MEME_CLASS_END()

MEME_CLASS(SetFocusAction)
  MEME_FIELD("x", Object)
  MEME_FIELD("y", Object)
MEME_CLASS_END()

MEME_CLASS(SetPathAction)
  MEME_FIELD("Path node", Object)
  MEME_FIELD("Destination node", Object)
  MEME_FIELD("In time", Float)
  MEME_FIELD("Out time", Float)
  MEME_FIELD("In wait time", Float)
  MEME_FIELD("Out wait time", Float)
  MEME_FIELD("Paint outnode over innode", Bool)
MEME_CLASS_END()

MEME_CLASS(SetStringAction)
  MEME_FIELD("Variable", Object)
  MEME_FIELD("Data", Object)
MEME_CLASS_END()

MEME_CLASS(SetTabstopAction)
  MEME_FIELD("Tabstop index", Object)
MEME_CLASS_END()

MEME_CLASS(SetVariableAction)
  MEME_FIELD("Variable", Object)
  MEME_FIELD("Data", Object)
MEME_CLASS_END()

MEME_CLASS(SetVariableArrayAction)
  MEME_FIELD("Variable", Object)
  MEME_FIELD("Data", Object)
  MEME_FIELD("Index", Object)
MEME_CLASS_END()

MEME_CLASS(SetVariableSineAction)
  MEME_FIELD("Variable", Object)
  MEME_FIELD("Data", Object)
  MEME_FIELD("Speed", Float)
  MEME_FIELD("Braking distance", Float)
MEME_CLASS_END()

MEME_CLASS(SetVariableSinusAction)
MEME_CLASS_END()

MEME_CLASS(SetVariableSoftAction)
  MEME_FIELD("Variable", Object)
  MEME_FIELD("Data", Object)
  MEME_FIELD("Speed", Float)
MEME_CLASS_END()

MEME_CLASS(ShowEffectNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Effect", Object)
MEME_CLASS_END()

MEME_CLASS(SinMoveEffect)
MEME_CLASS_END()

MEME_CLASS(SingleLineSizeStyle)
  MEME_FIELD("Font handle", Name)
  MEME_FIELD("Size constant", Float)
MEME_CLASS_END()

MEME_CLASS(SingleLineStyle)
  MEME_FIELD("Font handle", Name)
MEME_CLASS_END()

MEME_CLASS(SizeStyle)
  MEME_FIELD("Font handle", Name)
  MEME_FIELD("Size constant", Float)
MEME_CLASS_END()

MEME_CLASS(SliderNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Cursor node", Object)
  MEME_FIELD("Data", Object)
  MEME_FIELD("Minimum value", Float)
  MEME_FIELD("Maximum value", Float)
  MEME_FIELD("Number visible", Float)
MEME_CLASS_END()

MEME_CLASS(SoundAction)
  MEME_FIELD("Sound", Name)
MEME_CLASS_END()

MEME_CLASS(SplitAction)
  MEME_FIELD("Action 1", Object)
  MEME_FIELD("Action 2", Object)
MEME_CLASS_END()

MEME_CLASS(SplitEffect)
  MEME_FIELD("Effect 1", Object)
  MEME_FIELD("Effect 2", Object)
MEME_CLASS_END()

MEME_CLASS(SplitNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Split node", Object)
MEME_CLASS_END()

MEME_CLASS(StackData)
MEME_CLASS_END()

MEME_CLASS(StackPathNode)
  MEME_FIELD("Next node", Object)
MEME_CLASS_END()

MEME_CLASS(StaticFunction)
MEME_CLASS_END()

MEME_CLASS(StaticStringData)
MEME_CLASS_END()

MEME_CLASS(StickyNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Variable", Object)
  MEME_FIELD("Data", Object)
MEME_CLASS_END()

MEME_CLASS(Stream)
MEME_CLASS_END()

MEME_CLASS(StringData)
  MEME_FIELD("String <do not edit>", Unknown)
MEME_CLASS_END()

MEME_CLASS(StringRefData)
MEME_CLASS_END()

MEME_CLASS(Style)
  MEME_FIELD("Font handle", Name)
MEME_CLASS_END()

MEME_CLASS(SubData)
  MEME_FIELD("Data 1", Object)
  MEME_FIELD("Data 2", Object)
MEME_CLASS_END()

MEME_CLASS(System)
MEME_CLASS_END()

MEME_CLASS(TabstopIndexIterator)
MEME_CLASS_END()

MEME_CLASS(TabstopIterator)
MEME_CLASS_END()

MEME_CLASS(TabstopManagerNode)
MEME_CLASS_END()

MEME_CLASS(TabstopNode)
  MEME_FIELD("Next node", Object)
MEME_CLASS_END()

MEME_CLASS(TextNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("String", Object)
  MEME_FIELD("Style", Object)
MEME_CLASS_END()

MEME_CLASS(TimeoutActionNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Action", Object)
  MEME_FIELD("Timeout time", Float)
MEME_CLASS_END()

MEME_CLASS(ToModeIterator)
MEME_CLASS_END()

MEME_CLASS(ToggleData)
  MEME_FIELD("Toggle data", Object)
  MEME_FIELD("Data 1", Object)
  MEME_FIELD("Data 2", Object)
MEME_CLASS_END()

MEME_CLASS(ToggleVariableAction)
  MEME_FIELD("Variable", Object)
MEME_CLASS_END()

MEME_CLASS(TransformNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("X", Float)
  MEME_FIELD("Y", Float)
  MEME_FIELD("Width", Float)
  MEME_FIELD("Height", Float)
  MEME_FIELD("Transformed node", Object)
MEME_CLASS_END()

MEME_CLASS(TransitionalEffectNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Effect", Object)
MEME_CLASS_END()

MEME_CLASS(TransitionalHideEffectNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Effect", Object)
MEME_CLASS_END()

MEME_CLASS(TransitionalShowEffectNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Effect", Object)
MEME_CLASS_END()

MEME_CLASS(TranslateNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("X", Object)
  MEME_FIELD("Y", Object)
MEME_CLASS_END()

MEME_CLASS(Tree)
MEME_CLASS_END()

MEME_CLASS(TreeObject)
MEME_CLASS_END()

MEME_CLASS(TypeEvent)
  MEME_FIELD("Event type", Index)
MEME_CLASS_END()

MEME_CLASS(UnicodeEvent)
  MEME_FIELD("Event type", Index)
  MEME_FIELD("Unicode", Wchar)
MEME_CLASS_END()

MEME_CLASS(UnicodePressIterator)
MEME_CLASS_END()

MEME_CLASS(UnicodeReleaseIterator)
MEME_CLASS_END()

MEME_CLASS(UnlockFocusAction)
MEME_CLASS_END()

MEME_CLASS(UnlockPointerAction)
MEME_CLASS_END()

MEME_CLASS(VariableColorEffect)
  MEME_FIELD("Red", Object)
  MEME_FIELD("Green", Object)
  MEME_FIELD("Blue", Object)
  MEME_FIELD("Alpha", Object)
MEME_CLASS_END()

MEME_CLASS(VariableEffectNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Effect", Object)
  MEME_FIELD("Effect level", Object)
MEME_CLASS_END()

MEME_CLASS(VariablePictureNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("First part", Object)
  MEME_FIELD("Middle part", Object)
  MEME_FIELD("Last part", Object)
MEME_CLASS_END()

MEME_CLASS(VariableTabstopNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Variable", Object)
MEME_CLASS_END()

MEME_CLASS(Vertex)
MEME_CLASS_END()

MEME_CLASS(VertexColorEffect)
  MEME_FIELD("Top left red", Float)
  MEME_FIELD("Top left green", Float)
  MEME_FIELD("Top left blue", Float)
  MEME_FIELD("Top left alpha", Float)
  MEME_FIELD("Top right red", Float)
  MEME_FIELD("Top right green", Float)
  MEME_FIELD("Top right blue", Float)
  MEME_FIELD("Top right alpha", Float)
  MEME_FIELD("Bottom right red", Float)
  MEME_FIELD("Bottom right green", Float)
  MEME_FIELD("Bottom right blue", Float)
  MEME_FIELD("Bottom right alpha", Float)
  MEME_FIELD("Bottom left red", Float)
  MEME_FIELD("Bottom left green", Float)
  MEME_FIELD("Bottom left blue", Float)
  MEME_FIELD("Bottom left alpha", Float)
MEME_CLASS_END()

MEME_CLASS(VolumedSoundAction)
  MEME_FIELD("Sound", Name)
MEME_CLASS_END()

MEME_CLASS(WindowNode)
MEME_CLASS_END()

MEME_CLASS(WstackData)
MEME_CLASS_END()

MEME_CLASS(WstringData)
  MEME_FIELD("Wstring <do not edit>", Unknown)
MEME_CLASS_END()

MEME_CLASS(ZoomNode)
  MEME_FIELD("Next node", Object)
  MEME_FIELD("Z", Object)
MEME_CLASS_END()
