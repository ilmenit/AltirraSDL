// AT-SPI D-Bus interfaces from GNOME/at-spi2-core xml specifications.
// https://gitlab.gnome.org/GNOME/at-spi2-core/-/tree/main/xml
// Interface signatures are kept explicit to validate every exported request.
R"AT_SPI_XML(
<node><interface name="org.a11y.atspi.Accessible">
<property name="version" type="u" access="read" />
<property name="Name" type="s" access="read" />
<property name="Description" type="s" access="read" />
<property name="Parent" type="(so)" access="read">
</property>
<property name="ChildCount" type="i" access="read" />
<property name="Locale" type="s" access="read" />
<property name="AccessibleId" type="s" access="read" />
<property name="HelpText" type="s" access="read" />
<method name="GetChildAtIndex">
<arg direction="in" name="index" type="i" />
<arg direction="out" type="(so)" />
</method>
<method name="GetChildren">
<arg direction="out" type="a(so)" />
</method>
<method name="GetIndexInParent">
<arg direction="out" type="i" />
</method>
<method name="GetRelationSet">
<arg direction="out" type="a(ua(so))" />
</method>
<method name="GetRole">
<arg direction="out" type="u" />
</method>
<method name="GetRoleName">
<arg direction="out" type="s" />
</method>
<method name="GetLocalizedRoleName">
<arg direction="out" type="s" />
</method>
<method name="GetState">
<arg direction="out" type="au" />
</method>
<method name="GetAttributes">
<arg direction="out" type="a{ss}" />
</method>
<method name="GetApplication">
<arg direction="out" type="(so)" />
</method>
<method name="GetInterfaces">
<arg direction="out" type="as" />
</method>
</interface>
<interface name="org.a11y.atspi.Application">
<property name="ToolkitName" type="s" access="read" />
<property name="Version" type="s" access="read">
</property>
<property name="ToolkitVersion" type="s" access="read" />
<property name="AtspiVersion" type="s" access="read" />
<property name="InterfaceVersion" type="u" access="read" />
<property name="Id" type="i" access="readwrite" />
<method name="GetLocale">
<arg direction="in" name="lctype" type="u" />
<arg direction="out" type="s" />
</method>
<method name="GetApplicationBusAddress">
<arg direction="out" type="s" />
</method>
</interface>
<interface name="org.a11y.atspi.Text">
<property name="version" type="u" access="read" />
<property name="CharacterCount" type="i" access="read" />
<property name="CaretOffset" type="i" access="read" />
<method name="GetStringAtOffset">
<arg direction="in" name="offset" type="i" />
<arg direction="in" name="granularity" type="u" />
<arg direction="out" type="s" />
<arg direction="out" name="startOffset" type="i" />
<arg direction="out" name="endOffset" type="i" />
</method>
<method name="GetText">
<arg direction="in" name="startOffset" type="i" />
<arg direction="in" name="endOffset" type="i" />
<arg direction="out" type="s" />
</method>
<method name="SetCaretOffset">
<arg direction="in" name="offset" type="i" />
<arg direction="out" type="b" />
</method>
<method name="GetTextBeforeOffset">
<arg direction="in" name="offset" type="i" />
<arg direction="in" name="type" type="u" />
<arg direction="out" type="s" />
<arg direction="out" name="startOffset" type="i" />
<arg direction="out" name="endOffset" type="i" />
</method>
<method name="GetTextAtOffset">
<arg direction="in" name="offset" type="i" />
<arg direction="in" name="type" type="u" />
<arg direction="out" type="s" />
<arg direction="out" name="startOffset" type="i" />
<arg direction="out" name="endOffset" type="i" />
</method>
<method name="GetTextAfterOffset">
<arg direction="in" name="offset" type="i" />
<arg direction="in" name="type" type="u" />
<arg direction="out" type="s" />
<arg direction="out" name="startOffset" type="i" />
<arg direction="out" name="endOffset" type="i" />
</method>
<method name="GetCharacterAtOffset">
<arg direction="in" name="offset" type="i" />
<arg direction="out" type="i" />
</method>
<method name="GetAttributeValue">
<arg direction="in" name="offset" type="i" />
<arg direction="in" name="attributeName" type="s" />
<arg direction="out" type="s" />
</method>
<method name="GetAttributes">
<arg direction="in" name="offset" type="i" />
<arg direction="out" type="a{ss}" />
<arg direction="out" name="startOffset" type="i" />
<arg direction="out" name="endOffset" type="i" />
</method>
<method name="GetDefaultAttributes">
<arg direction="out" type="a{ss}" />
</method>
<method name="GetCharacterExtents">
<arg direction="in" name="offset" type="i" />
<arg direction="in" name="coordType" type="u" />
<arg direction="out" name="x" type="i" />
<arg direction="out" name="y" type="i" />
<arg direction="out" name="width" type="i" />
<arg direction="out" name="height" type="i" />
</method>
<method name="GetOffsetAtPoint">
<arg direction="in" name="x" type="i" />
<arg direction="in" name="y" type="i" />
<arg direction="in" name="coordType" type="u" />
<arg direction="out" type="i" />
</method>
<method name="GetNSelections">
<arg direction="out" type="i" />
</method>
<method name="GetSelection">
<arg direction="in" name="selectionNum" type="i" />
<arg direction="out" name="startOffset" type="i" />
<arg direction="out" name="endOffset" type="i" />
</method>
<method name="AddSelection">
<arg direction="in" name="startOffset" type="i" />
<arg direction="in" name="endOffset" type="i" />
<arg direction="out" type="b" />
</method>
<method name="RemoveSelection">
<arg direction="in" name="selectionNum" type="i" />
<arg direction="out" type="b" />
</method>
<method name="SetSelection">
<arg direction="in" name="selectionNum" type="i" />
<arg direction="in" name="startOffset" type="i" />
<arg direction="in" name="endOffset" type="i" />
<arg direction="out" type="b" />
</method>
<method name="GetRangeExtents">
<arg direction="in" name="startOffset" type="i" />
<arg direction="in" name="endOffset" type="i" />
<arg direction="in" name="coordType" type="u" />
<arg direction="out" name="x" type="i" />
<arg direction="out" name="y" type="i" />
<arg direction="out" name="width" type="i" />
<arg direction="out" name="height" type="i" />
</method>
<method name="GetBoundedRanges">
<arg direction="in" name="x" type="i" />
<arg direction="in" name="y" type="i" />
<arg direction="in" name="width" type="i" />
<arg direction="in" name="height" type="i" />
<arg direction="in" name="coordType" type="u" />
<arg direction="in" name="xClipType" type="u" />
<arg direction="in" name="yClipType" type="u" />
<arg direction="out" type="a(iisv)" />
</method>
<method name="GetAttributeRun">
<arg direction="in" name="offset" type="i" />
<arg direction="in" name="includeDefaults" type="b" />
<arg direction="out" type="a{ss}" />
<arg direction="out" name="startOffset" type="i" />
<arg direction="out" name="endOffset" type="i" />
</method>
<method name="GetDefaultAttributeSet">
<arg direction="out" type="a{ss}" />
</method>
<method name="ScrollSubstringTo">
<arg direction="in" name="startOffset" type="i" />
<arg direction="in" name="endOffset" type="i" />
<arg direction="in" name="type" type="u" />
<arg direction="out" type="b" />
</method>
<method name="ScrollSubstringToPoint">
<arg direction="in" name="startOffset" type="i" />
<arg direction="in" name="endOffset" type="i" />
<arg direction="in" name="coordType" type="u" />
<arg direction="in" name="x" type="i" />
<arg direction="in" name="y" type="i" />
<arg direction="out" type="b" />
</method>
</interface>
<interface name="org.a11y.atspi.Component">
<property name="version" type="u" access="read" />
<method name="Contains">
<arg direction="in" name="x" type="i" />
<arg direction="in" name="y" type="i" />
<arg direction="in" name="coord_type" type="u" />
<arg direction="out" type="b" />
</method>
<method name="GetAccessibleAtPoint">
<arg direction="in" name="x" type="i" />
<arg direction="in" name="y" type="i" />
<arg direction="in" name="coord_type" type="u" />
<arg direction="out" type="(so)" />
</method>
<method name="GetExtents">
<arg direction="in" name="coord_type" type="u" />
<arg direction="out" type="(iiii)" />
</method>
<method name="GetPosition">
<arg direction="in" name="coord_type" type="u" />
<arg direction="out" name="x" type="i" />
<arg direction="out" name="y" type="i" />
</method>
<method name="GetSize">
<arg direction="out" name="width" type="i" />
<arg direction="out" name="height" type="i" />
</method>
<method name="GetLayer">
<arg direction="out" type="u" />
</method>
<method name="GetMDIZOrder">
<arg direction="out" type="n" />
</method>
<method name="GrabFocus">
<arg direction="out" type="b" />
</method>
<method name="GetAlpha">
<arg direction="out" type="d" />
</method>
<method name="SetExtents">
<arg direction="in" name="x" type="i" />
<arg direction="in" name="y" type="i" />
<arg direction="in" name="width" type="i" />
<arg direction="in" name="height" type="i" />
<arg direction="in" name="coord_type" type="u" />
<arg direction="out" type="b" />
</method>
<method name="SetPosition">
<arg direction="in" name="x" type="i" />
<arg direction="in" name="y" type="i" />
<arg direction="in" name="coord_type" type="u" />
<arg direction="out" type="b" />
</method>
<method name="SetSize">
<arg direction="in" name="width" type="i" />
<arg direction="in" name="height" type="i" />
<arg direction="out" type="b" />
</method>
<method name="ScrollTo">
<arg direction="in" name="type" type="u" />
<arg direction="out" type="b" />
</method>
<method name="ScrollToPoint">
<arg direction="in" name="coord_type" type="u" />
<arg direction="in" name="x" type="i" />
<arg direction="in" name="y" type="i" />
<arg direction="out" type="b" />
</method>
</interface>
<interface name="org.a11y.atspi.Cache">
<property name="version" type="u" access="read" />
<method name="GetItems">
<arg direction="out" name="nodes" type="a((so)(so)(so)iiassusau)" />
</method>
<signal name="AddAccessible">
<arg name="nodeAdded" type="((so)(so)(so)iiassusau)" />
</signal>
<signal name="RemoveAccessible">
<arg name="nodeRemoved" type="(so)" />
</signal>
<signal name="Ready" />
</interface>
</node>
)AT_SPI_XML"
