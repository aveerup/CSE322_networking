for algorithm in ario-initial; do

    touch scratch/${algorithm}_out.csv
    if [[ "$algorithm" == "red" ]]; then
        echo "case,assured_rate,nSources,nFTP,nOnOff,avgQ,redMinTh,redMaxTh,redMaxP,redQW" > ./scratch/red_out.csv
    else
        echo "case,assured_rate,nSources,nFTP,nOnOFF,avgQ,marked_green,marked_yellow,marked_red,link_utilization,dropped_green,dropped_yellow,dropped_red" > ./scratch/${algorithm}_out.csv
    fi
    
    for case in 1 2 3 4 5 6; do
        for assured_rate in 25 50 75 100 125; do 
            for simTime in 40; do
                ./ns3 run "scratch/${algorithm}-simulation --case=${case} --assuredRate=${assured_rate} --simTime=${simTime}"
            done
        done
    done
done
