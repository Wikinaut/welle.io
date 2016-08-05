import QtQuick 2.0
import QtCharts 2.1

ChartView {
    id: spectrumView
    animationOptions: ChartView.NoAnimation
    theme: ChartView.ChartThemeDark
    backgroundColor: "#00000000"
    //titleColor: "white"
    legend.visible: false
    title: "Spectrum"

    property real maxYAxis: 0

    Connections{
        target: cppGUI
        onMaxYAxisChanged:{

            if(axisY1.max < max) // Up scale y axis emidetly if y should be bigger
            {
                axisY1.max = max
            }
            else // Only for down scale
            {
                yAxisMaxTimer.running = true
                maxYAxis = max
            }
        }
    }

    ValueAxis {
        id: axisY1
        min: 0
        titleText: "Amplitude"
    }


    ValueAxis {
        id: axisX
        min: 0
        max: 2048
        titleText: "Dips"
    }

    LineSeries {
        id: lineSeries1
        axisX: axisX
        axisY: axisY1
    }

    Timer {
        id: refreshTimer
        interval: 1 / 25 * 1000 // 25 Hz
        running: parent.visible ? true : false // Trigger new data only if spectrum is showed
        repeat: true
        onTriggered: {
           cppGUI.updateSpectrum(spectrumView.series(0));
            //console.log("test\n")
        }
    }

    Timer {
        id: yAxisMaxTimer
        interval: 10 * 1000 // 10 s
        //running: true
        repeat: false
        onTriggered: {
           axisY1.max = maxYAxis
        }
    }
}
