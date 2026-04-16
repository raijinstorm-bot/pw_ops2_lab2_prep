Name and surname: Shared memory and memory mapping
Stage 1 2 3 4 Total
Points 6 8 6 5 25
Score
L7: Bitronka
It is the year 2025. In Bitronka | a modern retail empire | chaos no longer reigns. War does. In the
heat of the daily struggle for wholegrain rolls, customers create disorder on store shelves. They swap
chips with yogurts, shove apples between pears. Chewing gum? Best placed next to butter. A can of
pineapple? Why not let it land in the dairy section? The worst of them all is Mrs. Hermina, who every
day moves AA batteries to the frozen foods section because \it's cooler there."
However, lurking between the shelves is an even greater threat than shelf disorder | pallets. Dozens,
hundreds, entire towers of pallets are stacked up to the ceiling | shaky and unstable. Maliciously, they
stand across aisles and block evacuation routes. For a new employee, one careless step in the aisle can
end tragically | a fallen pallet often means the end of the shift. . . forever.
In the evenings, when the last customer leaves the store with a bag full of candies weighed as onions,
the workers begin the cleanup operation. They try to restore order. \These batteries are next to frozen
food again?!" someone shouts, and the echo carries all the way to the fruit section. They know that
organizing the store is a losing battle, because chaos will return with the rst opening of the doors the
next morning. But they do not give up. Each of them randomly selects a pair of products and evaluates
whether their placement needs to change. If so | the products are moved to the proper (or at least more
logical) place. Of course, as long as no one trips over a protruding pallet along the way. Overseeing the
cleanup is the shift manager, who watches in disbelief as his team ghts the same, already lost battle
against entropy every day.
Your task is to write a simulation of a cleanup shift that tries to restore order at night | even if only
for a moment. The simulation must be implemented using shared memory and processes.
Stages:
1. 6 pts. The program takes two parameters: 8 N 256 | the number of products, and 1 M
2. 64 | the number of workers. The store is represented as a one-dimensional array of size N , where
each index represents a shelf, and its value is the current product (an integer from 1 to N ).
Create a le SHOP FILENAME and map it into the process memory, then initialize the array representing
store shelves there. Fill it with consecutive integers and shue it using the provided shuffle method.
Using streams and the read function is forbidden.
Before exiting the program, print the contents of the product array using the provided print array
method.
8 pts. Create M worker processes. Each worker, upon creation, prints [PID] Worker reports for
a night shift.
After reporting in, each worker randomly selects two dierent indices from the range [1; N ] ten times.
If the products at these indices are in the wrong order (i.e., the value at the rst index is greater
than at the second), they swap them to improve order on the shelves, sleeping for 100ms during the
swap. After nishing, the worker process terminates.
Map anonymous shared memory in the process, where mutexes protecting access to the array values
will be stored (one mutex per shelf). Place other shared variables in the same shared memory
as well.
Print the contents of the product array before starting worker processes. After all worker processes
nish, do the same again, but also print Night shift in Bitronka is over before exiting the
program.
Name and surname: 3. 6 pts. 4. Shared memory and memory mapping
After creating worker processes, create a manager process. Upon reporting in, the manager
prints [PID] Manager reports for a night shift. The manager process prints the array contents
every half second, synchronizes the le with its current state, and checks order. If it is sorted, it prints
[PID] The shop shelves are sorted and announces the end of work to the workers by setting a
variable in shared memory. Then it terminates.
Workers continue working until the manager announces the end of the shift.
In worker processes, add a 1% chance of sudden death just before swapping products (use
the abort function for this purpose). Before dying, they only print [PID] Trips over pallet and
dies. Living workers and the manager count the number of alive (or dead) workers in shared memory.
Upon nding a dead worker, they print [PID] Found a dead body in aisle [SHELF INDEX]. Note:
Observe that a body may be found twice. After printing the array state, the manager prints the
number of alive workers: [PID] Workers alive: [ALIVE COUNT]. When there are no living worker
processes left, the manager prints [PID] All workers died, I hate my job and terminates.
5 pts. 