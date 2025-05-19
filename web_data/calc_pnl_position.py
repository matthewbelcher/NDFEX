
def main(argv):
    with open(argv[0], 'r') as file:
        lines = file.readlines()

    positions = {1: 0, 2: 0}
    total_buy = {1: 0, 2: 0}
    total_sell = {1: 0, 2: 0}
    volume = {1: 0, 2: 0}
    last_price = {1: 0, 2: 0}
    pnl = {1: 0, 2: 0}

    for line in lines:
        split_line = line.split()
        side = int(split_line[-3])
        price = int(split_line[-5])
        quantity = int(split_line[-7])
        symbol = int(split_line[-9])

        if side == 1:
            positions[symbol] += quantity
            total_buy[symbol] += price * quantity
        elif side == 2:
            positions[symbol] -= quantity
            total_sell[symbol] += price * quantity
        volume[symbol] += quantity

        if positions[symbol] == 0:
            print ('Pos {} PNL for symbol {}: {}'.format(positions[symbol], symbol, total_sell[symbol] - total_buy[symbol] - 0.05 * volume[symbol]))
            pnl[symbol] = total_sell[symbol] - total_buy[symbol] - 0.05 * volume[symbol]
        else:
            # mark positions to last fill price
            if positions[symbol] > 0:
                temp_total_sell = total_sell[symbol] + (price - 20) * positions[symbol]
                print ('Pos {} PNL for symbol {}: {}'.format(positions[symbol], symbol, temp_total_sell - total_buy[symbol] - 0.05 * volume[symbol]))
                pnl[symbol] = temp_total_sell - total_buy[symbol] - 0.05 * volume[symbol]
            else:
                temp_total_buy = total_buy[symbol] + (price + 20) * abs(positions[symbol])
                print ('Pos {} PNL for symbol {}: {}'.format(positions[symbol], symbol, total_sell[symbol] - temp_total_buy - 0.05 * volume[symbol]))
                pnl[symbol] = total_sell[symbol] - temp_total_buy - 0.05 * volume[symbol]

        last_price[symbol] = price
        if abs(positions[symbol]) > 50:
            print (f'BUST: {positions[symbol]} {pnl[1] + pnl[2]}')
            print (f'Volume: {volume[1] + volume[2]}')
            return

        if pnl[1] + pnl[2] < -10000:
            print ('BUST: {} {}'.format(pnl[1] + pnl[2], positions[1] + positions[2]))
            print (f'Volume: {volume[1] + volume[2]}')

            return

    for symbol, position in positions.items():
        if position > 0:
            print(f"Long position in symbol {symbol}: {position}")
        elif position < 0:
            print(f"Short position in symbol {symbol}: {abs(position)}")
        else:
            print(f"No position in symbol {symbol}")


    print (f"Final PNL: {pnl[1] + pnl[2]}")
    print (f"Total Volume: {volume[1] + volume[2]}")
if __name__ == "__main__":
    import sys
    main(sys.argv[1:])