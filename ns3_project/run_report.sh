for algorithm in rio-wireless-report; do

    touch scratch/${algorithm}_out.csv

    if [[ "$algorithm" == "rio-wireless-report" || "$algorithm" == "ario-wireless-report" ]]; then
        echo "case,assured_rate,nSources,nFlows,pps,mobility,nodespeed,txrange,area_multiplier,nFtp,nOnOff,avgQ,throughput,avg_delay,delivery_ratio,drop_ratio,energy_consumed" > ./scratch/${algorithm}_out.csv
    else
        echo "case,assured_rate,nSources,nFlows,pps,avgQ,throughput,avg_delay,delivery_ratio,drop_ratio" > ./scratch/${algorithm}_out.csv
    fi
    
    for case in 3; do
        for assured_rate in 100; do 
            for simTime in 40; do
                for nNodes in 20 40 60 80 100; do
                    ./ns3 run "scratch/${algorithm}-simulation --case=${case} --assuredRate=${assured_rate} --simTime=${simTime} --nNodes=${nNodes} --nFlows=${nNodes} --pps=100"
                done
            done
        done
    done

    for case in 3; do
        for assured_rate in 100; do 
            for simTime in 40; do
                for nFlows in 10 20 30 40 50; do
                    ./ns3 run "scratch/${algorithm}-simulation --case=${case} --assuredRate=${assured_rate} --simTime=${simTime} --nNodes=50 --nFlows=${nFlows} --pps=100"
                done 
            done
        done
    done


    for case in 3; do
        for assured_rate in 100; do 
            for simTime in 40; do
                for pps in 100 200 300 400 500; do
                    ./ns3 run "scratch/${algorithm}-simulation --case=${case} --assuredRate=${assured_rate} --simTime=${simTime} --nNodes=50 --nFlows=50 --pps=${pps}"
                done 
            done
        done
    done
done
