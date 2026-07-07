import QtQuick 2.0
import Sailfish.Silica 1.0

// A term heading with an explanatory paragraph, used on the glossary page.
Column {
    property alias term: heading.text
    property alias body: text.text

    x: Theme.horizontalPageMargin
    width: parent.width - 2 * Theme.horizontalPageMargin
    spacing: Theme.paddingSmall

    Label {
        id: heading
        width: parent.width
        wrapMode: Text.Wrap
        font.pixelSize: Theme.fontSizeSmall
        color: Theme.highlightColor
    }
    Label {
        id: text
        width: parent.width
        wrapMode: Text.Wrap
        font.pixelSize: Theme.fontSizeExtraSmall
        color: Theme.secondaryColor
    }
}
