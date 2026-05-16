'Libraries
Import mojo 'Contains several necessary libraries such as graphics
Import monkey.random 'Used for random number generation
Import brl 'Used for file handling
Import os 'Used for exiting app
'Globals
Global Game:Game_app 'Game
Global PlayerNameArray:String[11] 'Names are kept here.
Global PlayerScoreArray:Int[11] 'Scores are kept here.
Global CurrentMusic:String = "MENU" 'Used to indicate which music is currently selected.
Global MenuMusic:Sound 'Variable to store menu music
Global Level1Music:Sound 'Variable to store level 1 music
Global Level2Music:Sound 'Variable to store level 2 music
Global Level3Music:Sound 'Variable to store level 3 music

'Main program starts here:
Function Main ()
	Game = New Game_app
End

'The main program.
Class Game_app Extends App
	'Main variables
	Global GameState:String = "NAME" 'Used to traverse the various parts of the game.
	Global CurrentLevel:Int = 1 'There are three playable levels: 1,2,3.
	Global CurrentScore:Int = 0 'The player accumulates points based on their actions.
	Global TimeElapsed:Int = 0 'How long a player has taken to complete the three levels.
	Global FramesElapsed:Int = 0 'Incremented by one every frame. Used to calculate time elapsed.
	'GameState images
	Field MenuScreen:Image 'The menu image
	Field GameoverScreen:Image 'The gameover image
	Field LeaderboardScreen:Image 'The leaderboard image.
	Field WinnerScreen:Image 'The winner screen.
	Field StoryScreen:Image 'The story screen.
	Field HelpScreen:Image 'The help screen.
	'Level Images
	Field Level1Background:Image 'Level1
	Field Level2Background:Image 'Level2
	Field Level3Background:Image 'Level3
	'Characters
	Field player:Hero 'The player character, controlled by the player
	Field FirstEnemy:Skeleton 'The first enemy.
	Field SecondEnemy:Ninja 'The second enemy.
	Field ThirdEnemy:BlueCloak 'The third enemy
	'Smoke object
	Field Smoke:SmokeEffect 'Used for dodging.
	'User Interface while fighting
	Field HealthInterface:Healthbars 'Used for the player and enemy health
	'Used for handling name.
	Field NameScreen:Image 'The namescreen image.
	Field ReceivedText:String 'Where the user submits their name
	Field CharacterLimit:Int 'How many characters long the user's name can be.
	'Sounds
	Field VictoryTheme:Sound 'Played when the player wins.
	Field GameoverTheme:Sound 'Played when the player dies.
	
	'Initialization occurs in the OnCreate() method.
	Method OnCreate ()
		SetUpdateRate 60 'Game Refresh Rate (number of frames per second) is set to 60.
		SetFont = LoadImage("slateFont.png", 16, 16, 64)
		'Gamestate Images
		MenuScreen = LoadImage("menu.png") 'Our menu cover.
		GameoverScreen = LoadImage("gameover.png") 'Our gameover cover.
		LeaderboardScreen = LoadImage("leaderboardscreen.png") 'Our leaderboard cover.
		WinnerScreen = LoadImage("WinnerScreen.png") 'Our winner screen.
		NameScreen = LoadImage("namescreen.png") 'Our name input cover.
		StoryScreen = LoadImage("StoryScreen.png") 'The story of the game
		HelpScreen = LoadImage("HelpScreen.png") 'The help screen.
		'Level images
		Level1Background = LoadImage("Level1.png") 'Level1
		Level2Background = LoadImage("Level2.png") 'Level2
		Level3Background = LoadImage("Level3.png") 'Level3
		'Characters
		player = New Hero 'The player character, controlled by the player, is initialized.
		FirstEnemy = New Skeleton() 'The first enemy is initialized
		SecondEnemy = New Ninja() 'The second enemy is initialized
		ThirdEnemy = New BlueCloak() 'The third enemy is initialized
		'Smoke
		Smoke = New SmokeEffect() 'Used for dodging
		'User Interface while fighting
		HealthInterface = New Healthbars()
		'Initializing variables used for entering name.
		ReceivedText = ""
		CharacterLimit = 10
		'Initializing the arrays used for the leaderboard.
		For Local Index:=0 Until 10
			PlayerScoreArray[Index] = 0
			PlayerNameArray[Index] = ""
		Next
		'Sounds
		VictoryTheme = LoadSound("Victory.ogg")
		GameoverTheme = LoadSound("Gameover.ogg")
		'Music tracks (menu)
		MenuMusic = LoadSound("TrackMenu.ogg")
		'Music tracks (in game)
		Level1Music = LoadSound("TrackLevel1.ogg")
		Level2Music = LoadSound("TrackLevel2.ogg")
		Level3Music = LoadSound("TrackLevel3.ogg")
	End
	
	'All game logic goes in the OnUpdate() method.
	Method OnUpdate ()
		Select GameState
			Case "MENU" 'All gamestates can be accessed from here.
				RequestMusic("MENU") 'Play the menu track
				If KeyHit(KEY_SPACE) Then GameState = "PLAYING" 'Gameplay
				If KeyHit(KEY_N) Then GameState = "NAME" 'Enter a name
				If KeyHit(KEY_L) Then GameState = "LEADERBOARD" 'Display leaderboard
				If KeyHit(KEY_H) Then GameState = "HELP" 'Display help.
				If KeyHit(KEY_S) Then GameState = "STORY" 'Display the story.
				If KeyHit(KEY_X) Then ExitApp(0) 'Exit the application.
			Case "NAME" 'Name is entered here.
				RequestMusic("MENU") 'Play the menu track
				If KeyHit(KEY_ENTER) And ReceivedText.Length > 0 Then GameState = "MENU"
				While ReceivedText.Length < CharacterLimit 'Prevent letters from being typed when over character limit
					Local currentChar = GetChar() 'Receiving input
					If Not currentChar Exit
					If currentChar >= 32 Then 'Selectring specific characters from ASCII set
						ReceivedText += String.FromChar(currentChar) 'add the letter to received text.
					End
				End While
				
				If KeyHit(KEY_BACKSPACE) Then 'When the user presses backspace.
					'If received text isn't empty, remove the last character.
					If ReceivedText.Length > 0 Then ReceivedText = ReceivedText[0..ReceivedText.Length-1] 
				End
			Case "LEADERBOARD" 'Leaderboard is displayed here.
				RequestMusic("MENU") 'Play the menu track
				If KeyHit(KEY_ESCAPE) Then GameState = "MENU"
				SortHighScores()
			Case "WINNER" 'Displayed when the player beats the last level.
				RequestMusic("NONE") 'No music.
				If KeyHit(KEY_ESCAPE) Then
					GameState = "MENU"
					WriteToLeaderboard(CurrentScore, ReceivedText)
					'Reset the variables used for calculating the score
					FramesElapsed = 0
					TimeElapsed = 0
					'Reset variable used for storing the score.
					CurrentScore = 0
					StopChannel(0) 'Stop the victory theme if it is still playing.
				End
				If KeyHit(KEY_L) Then 
					GameState = "LEADERBOARD"
					WriteToLeaderboard(CurrentScore, ReceivedText)
					'Reset the variables used for the score
					FramesElapsed = 0
					TimeElapsed = 0
					CurrentScore = 0
					StopChannel(0) 'Stop the victory theme if it is still playing.
				End
			Case "HELP" 'Help on how to play the game is displayed here.
				RequestMusic("MENU") 'Play the menu track
				If KeyHit(KEY_ESCAPE) Then GameState = "MENU"
			Case "STORY" 'The story of the game is displayed here.
				RequestMusic("MENU") 'Play the menu track
				If KeyHit(KEY_ESCAPE) Then GameState = "MENU"
			Case "GAMEOVER" 'For when the player dies.
				RequestMusic("NONE") 'No music.
				If KeyHit(KEY_SPACE) Then
					GameState = "PLAYING" 'Back to playing state
					player.Reset() 'Reset player
					FirstEnemy.Reset() 'Reset enemy
					SecondEnemy.Reset() 'Reset second enemy
					ThirdEnemy.Reset() 'Reset third enemy.
					StopChannel(0) 'Stop the gameover theme if it is still playing
				End
			Case "PLAYING" 'The gameplay occurs here.
				'============[General Logic]==============
				If KeyHit(KEY_ESCAPE) Then GameState = "MENU"
				'Check if the player is dead
				If player.State = "DEAD" Then
					GameState = "GAMEOVER" 
					PlaySound(GameoverTheme, 0, 0) 'Play the gameover theme within AudioChannel 0, just once.
				End
				'Calculate the time taken to complete the level.
				FramesElapsed += 1 'The number of frames that have passed since starting the level
				TimeElapsed = FramesElapsed / 60 'There are 60 frames in each second. 
				'==========[Level Specific Logic]============
				Select CurrentLevel
					Case 1 'Level 1 logic occurs here.
						RequestMusic("LEVEL1") 'Play the level 1 music
						'Updating health bars
						HealthInterface.UpdateBars(player.Health, 100, player.MagicEnergy, 100, FirstEnemy.Health, 300, player.FlameWarriorDuration, 600)
						'Player logic.
						player.Update(KeyDown(KEY_LEFT), KeyDown(KEY_RIGHT), KeyDown(KEY_UP), KeyDown(KEY_DOWN), KeyDown(KEY_SPACE), KeyDown(KEY_Q), FirstEnemy.x+36, FirstEnemy.y, 96, 192)
						If player.State = "TRANSFORM" Then 'Having the enemy pause while the transformation occurs.
							FirstEnemy.State = "IDLE"
							FirstEnemy.CooldownTime = 2 'This code is executed for every frame that the player is in the transform state. A cooldown time of 2 is therefore all that is necessary to keep the enemy idle.
							FirstEnemy.dx = 0 'The enemy should not be able to move while idle.
						End
						'Enemy logic.
						FirstEnemy.Update(player.x, player.y, player.Health)
						'Handle damage between player and opponent.
						If player.PendingDamageDealt > 0 Then 'If the player has successfully dealt damage to the enemy
							FirstEnemy.ReceiveDamage(player.PendingDamageDealt, False) 'Pass this damage to the enemy through receivedamage method.
							player.PendingDamageDealt -= player.Damage 'Remove this pending damage.
						End
						If FirstEnemy.PendingDamageDealt > 0 Then 'If the enemy has successfully dealt damage to the player
							player.ReceiveDamage(FirstEnemy.PendingDamageDealt) 'Pass this damage to the enemy through receivedamage method.
							FirstEnemy.PendingDamageDealt -= FirstEnemy.Damage 'Remove this pending damage.
						End
						'Check if level has been beaten.
						If FirstEnemy.State = "DEAD" Then 'If enemy is completely dead.
							CurrentLevel = 2 'Move to next level.
							player.Reset() 'Reset player and enemy
							FirstEnemy.Reset()
							'Adding to the player's score. This gets lower the longer they take. 
							CurrentScore += 1000 / TimeElapsed 
							FramesElapsed = 0
							TimeElapsed = 0 'Resetting time elapsed.
						End
					Case 2 'Level 2 logic occurs here.
						RequestMusic("LEVEL2") 'Play the level 2 music
						'Updating health bars
						HealthInterface.UpdateBars(player.Health, 100, player.MagicEnergy, 100, SecondEnemy.Health, 400, player.FlameWarriorDuration, 600)
						'Player logic.
						player.Update(KeyDown(KEY_LEFT), KeyDown(KEY_RIGHT), KeyDown(KEY_UP), KeyDown(KEY_DOWN), KeyDown(KEY_SPACE), KeyDown(KEY_Q), SecondEnemy.x+35, SecondEnemy.y+5, 70, 145)
						If player.State = "TRANSFORM" Then 'Having the enemy pause while the transformation occurs.
							SecondEnemy.State = "IDLE"
							SecondEnemy.CooldownTime = 2 'This code is executed for every frame that the player is in the transform state. A cooldown time of 2 is therefore all that is necessary to keep the enemy idle.
							SecondEnemy.dx = 0 'The enemy should not be able to move while idle.
						End
						'Enemy logic.
						SecondEnemy.Update(player.x, player.y, player.Health)
						'Handle damage between player and opponent.
						If player.PendingDamageDealt > 0 Then 'If the player has successfully dealt damage to the enemy
							SecondEnemy.ReceiveDamage(player.PendingDamageDealt, False) 'Pass this damage to the enemy through receivedamage method.
							player.PendingDamageDealt -= player.Damage 'Remove this pending damage.
						End
						If SecondEnemy.PendingDamageDealt > 0 Then 'If the enemy has successfully dealt damage to the player
							player.ReceiveDamage(SecondEnemy.PendingDamageDealt) 'Pass this damage to the enemy through receivedamage method.
							SecondEnemy.PendingDamageDealt -= SecondEnemy.Damage 'Remove this pending damage.
						End
						If SecondEnemy.State = "DEAD" Then 'If enemy is completely dead.
							CurrentLevel = 3 'Move to next level.
							player.Reset() 'Reset player and enemy
							SecondEnemy.Reset()
							'Adding to the player's score. This gets lower the longer they take. 
							CurrentScore += 10000 / TimeElapsed 
							FramesElapsed = 0
							TimeElapsed = 0 'Resetting time elapsed.
						End	
					Case 3 'Level 3 logic occurs here.
						RequestMusic("LEVEL3") 'Play the level 3 music
						'Updating health bars
						HealthInterface.UpdateBars(player.Health, 100, player.MagicEnergy, 100, ThirdEnemy.Health, 600, player.FlameWarriorDuration, 600)
						'Player logic.
						player.Update(KeyDown(KEY_LEFT), KeyDown(KEY_RIGHT), KeyDown(KEY_UP), KeyDown(KEY_DOWN), KeyDown(KEY_SPACE), KeyDown(KEY_Q), ThirdEnemy.x+35, ThirdEnemy.y+15, 75, 100)
						If player.State = "TRANSFORM" Then 'Having the enemy pause while the transformation occurs.
							ThirdEnemy.State = "IDLE"
							ThirdEnemy.CooldownTime = 2 'This code is executed for every frame that the player is in the transform state. A cooldown time of 2 is therefore all that is necessary to keep the enemy idle.
							ThirdEnemy.dx = 0 'The enemy should not be able to move while idle.
						End
						'Enemy logic.
						ThirdEnemy.Update(player.x, player.y)
						'Handle damage between player and opponent.
						If player.PendingDamageDealt > 0 Then 'If the player has successfully dealt damage to the enemy
							Local DodgeChance = Rnd(1,6)
							If DodgeChance = 1 Then
								Smoke.SmokeEnabled = True 'Smoke is active.
								Smoke.UpdatePosition(ThirdEnemy.x-20, ThirdEnemy.y-30)
								ThirdEnemy.ReceiveDamage(player.PendingDamageDealt, True) 'Pass this damage to the enemy through receivedamage method.
							Else
								ThirdEnemy.ReceiveDamage(player.PendingDamageDealt, False) 'The enemy will dodge this attack.
							End
							player.PendingDamageDealt -= player.Damage 'Remove this pending damage.
						End
						If ThirdEnemy.PendingDamageDealt > 0 Then 'If the enemy has successfully dealt damage to the player
							player.ReceiveDamage(ThirdEnemy.PendingDamageDealt) 'Pass this damage to the enemy through receivedamage method.
							ThirdEnemy.PendingDamageDealt -= ThirdEnemy.Damage 'Remove this pending damage.
						End
						If ThirdEnemy.State = "DEAD" Then 'If the enemy has completely died
							CurrentLevel = 1
							player.Reset() 'Reset the player back to the original state
							ThirdEnemy.Reset() 'Reset the enemy back to the original state.
							GameState = "WINNER"
							PlaySound(VictoryTheme, 0, 0) 'Play the victory theme within AudioChannel 0, just once.
							'Adding to the player's score. This gets lower the longer they take. 
							CurrentScore += 50000 / TimeElapsed 
							FramesElapsed = 0
							TimeElapsed = 0 'Resetting time elapsed.
						End
				End
		End Select
	End
	
	'Everything is rendered to the screen in the OnRender() method.
	Method OnRender ()
		Select GameState
			Case "MENU"
				DrawImage MenuScreen, 0,0 'Menu image is displayed.
			Case "NAME" 'Name is entered here
				DrawImage NameScreen, 0,0 'Our name screen is drawn
				DrawText ">" + ReceivedText.ToUpper() + "<", 586 - (ReceivedText.Length*7), 350 'Shows the text input by the user.
			Case "LEADERBOARD" 'Leaderboard is displayed here.
				DrawImage LeaderboardScreen, 0,0 'Our leaderboard image is drawn.
				'Displaying the top 10 scores.
				For Local Rank:= 0 Until 10
					DrawText("  " + (Rank+1) + ") ", 360, 210 + Rank * 45) 'Draw rank numbers
					DrawText(PlayerScoreArray[Rank], 500, 210 + Rank *45) 'Draw scores
					DrawText(PlayerNameArray[Rank].ToUpper(), 660, 210 + Rank*45) 'Draw names
				Next
			Case "WINNER" 'Occurs when the player beats the last level
				DrawImage WinnerScreen, 0,0 'Our winner image is drawn.
				DrawText CurrentScore, 568, 415 'Final score is displayed to user.
			Case "HELP" 'Help is displayed here.
				DrawImage HelpScreen, 0,0
			Case "STORY" 'Story is displayed here.
				DrawImage StoryScreen, 0,0
			Case "GAMEOVER" 'For when the player dies
				DrawImage GameoverScreen, 0,0
			Case "PLAYING" 'Rendering of the main gameplay occurs here.
				'Level specific drawing occurs here.
				If CurrentLevel = 1 Then
					DrawImage Level1Background, 0,0 'Level1
					FirstEnemy.Animate() 'Animates the skeleton
					DrawImage FirstEnemy.CurrentSprite, FirstEnemy.x, FirstEnemy.y 'Draws the skeleton at their coordinates
					'Enemy Health Portrait
					DrawImage HealthInterface.SkeletonPortrait, 871, 0
				Elseif CurrentLevel = 2 Then 
					DrawImage Level2Background, 0,0 'Level2
					SecondEnemy.Animate() 'Animates the ninja
					'Draw the ninja at their coordinates if they are not attacking.
					If SecondEnemy.State <> "ATTACK" And SecondEnemy.State <> "HIT" Then DrawImage SecondEnemy.CurrentSprite, SecondEnemy.x, SecondEnemy.y 
					'Draw ninja at offset coordinates if they are hit
					If SecondEnemy.State = "HIT" Then DrawImage SecondEnemy.CurrentSprite, SecondEnemy.x, SecondEnemy.y-12
					'Draw the ninja at offset coordinates if they are attacking.
					If SecondEnemy.State = "ATTACK" And SecondEnemy.Direction = "LEFT" Then DrawImage SecondEnemy.CurrentSprite, SecondEnemy.x-144, SecondEnemy.y -39
					If SecondEnemy.State = "ATTACK" And SecondEnemy.Direction = "RIGHT" Then DrawImage SecondEnemy.CurrentSprite, SecondEnemy.x-45, SecondEnemy.y -39
					'Enemy Health Portrait
					DrawImage HealthInterface.NinjaPortrait, 871, 0
				Elseif CurrentLevel = 3 Then 
					DrawImage Level3Background, 0,0 'Level3
					ThirdEnemy.Animate() 'Animates the third enemy (blue cloak)
					DrawImage ThirdEnemy.CurrentSprite, ThirdEnemy.x, ThirdEnemy.y 'Draws the third enemy at their coordinates
					'Enemy Health Portrait
					DrawImage HealthInterface.BlueCloakPortrait, 871, 0
					'Enemy smoke effects when dodging
					Smoke.HandleSmoke()
					DrawImage Smoke.CurrentSmoke, Smoke.x, Smoke.y
				End
				'Player Health Interface
				If player.FlameWarrior = True Then 'If flame warrior is active
					DrawImage HealthInterface.FlameWarriorPortrait, 0,0 'Render special portrait
					DrawImage HealthInterface.PlayerActiveEnergy_Current, 106,68 'Render orange energy bar
				Else 'Normal warrior
					DrawImage HealthInterface.WarriorPortrait, 0,0 'Render normal portrait
					DrawImage HealthInterface.PlayerEnergy_Current, 106, 68 'Render normal energy bar
				End
				DrawImage HealthInterface.PlayerHealth_Current, 105,32
				'Enemy health interface
				DrawImage HealthInterface.EnemyHealth_Current, 925+(174-HealthInterface.EnemyHealth_Current.Width), 32
				DrawImage HealthInterface.EnemyEnergy_Full, 926, 68
				'Information to render to the screen.
				DrawText "NAME: " + ReceivedText.ToUpper(), 550, 20 'Name
				DrawText "LEVEL: " + CurrentLevel, 550,34
				DrawText "SCORE: " + CurrentScore, 550,48
				DrawText "TIME: " + TimeElapsed, 550, 62
				'Player animation and rendering
				player.Animate() 'Animates the player
				DrawImage player.CurrentSprite, player.x, player.y 'Draws the player at their coordinates.				
		End Select
	End
	
End

'=================[ Object Classes ]====================

Class Hero
	'Position Variables
	Field x:Int = 60 'x position of player
	Field y:Int = 407 'y position of player
	'Movement variables
	Field dx:Int = 0 'Horizontal speed. 'dx' is a mathematical term meaning 'change in x'
	Field dy:Int = 0 'Vertical speed. 'dy' is a mathematical term meaning 'change in y'
	'Stats
	Field Speed:Int = 6 'Used for easily modifying player speed
	Field Health:Int = 100 'How much health the player has
	Field Damage:Int = 20 'How much damage the player can deal
	Field MagicEnergy:Int = 0 'How much magic energy the player currently has. This is used for activating the flame warrior mode.
	'Logic Variables
	Field FlameWarrior:Bool = False 'When active, the player is drawn as the flame warrior and has increased damage, speed and heals back to full health
	Field FlameWarriorDuration:Int = 0 'When flame warrior is activated, this is set to a number. It decrements until 0. When it hits 0, normal mode is activated.
	Field CurrentSprite:Image 'The image currently inside this is drawn in the OnRender method.
	Field AnimationTick:Int = 0 'Increments by 1 every frame. Used to decide how long a sprite can appear before the next one should appear. 
	Field State:String = "IDLE" 'The current state of the player. Used to decide what animations should play, and what movement/attack abilities should work.
	Field Direction:String = "RIGHT" 'The current direction being faced by the player. Used to decide whether the left or right animations play.
	Field Midair:Bool = False 'Used to identify when the player is currently off the ground and is midair.
	Field AttackStage:Int = 0  '0 = Not currently attacking. 1 = Attack1 is active. 2 = Attack2 is active. 3 = Attack3 is active.
	Field PendingDamageDealt:Int = 0 'Holds the damage that has successfully landed on an enemy
	Field CanTransferDamage:Bool = True 'Is disabled the moment that pending damage is added. Can be re-enabled by changing attack stage.
	Field AttackCooldown:Int = 0 'Decrements by 1 every frame when greater than 0. 
	'Sounds
	Field Swing1:Sound 'Low pitch normal swing
	Field Swing2:Sound 'Medium pitch normal swing
	Field Swing3:Sound 'High pitch normal swing
	Field FireSwing1:Sound 'Low pitch flame warrior swing
	Field FireSwing2:Sound 'Medium pitch flame warrior swing
	Field FireSwing3:Sound 'High pitch flame warrior swing
	Field TransformStart:Sound 'Beginning of transformation
	Field TransformFinish:Sound 'Transformation finished
	Field Jumping:Sound 'When the player jumps
	Field Landing:Sound 'When the player lands on the floor.
	Field Damaged1:Sound 'When the player is hit (high pitch)
	Field Damaged2:Sound 'When the player is hit (low pitch).
	'Sprite sheets
	Field LeftSheet:Image 'The normal warrior mode.
	Field RightSheet:Image
	Field Fire_LeftSheet:Image 'The flame warrior mode.
	Field Fire_RightSheet:Image
	'Arrays for storing sprites (Normal Mode)
	Field IdleLeft:Image[8] 'Idle
	Field IdleRight:Image[8]
	Field RunLeft:Image[8] 'Run
	Field RunRight:Image[8]
	Field Attack1Left:Image[4] 'Left facing attacks
	Field Attack2Left:Image[4]
	Field Attack3Left:Image[5]
	Field Attack1Right:Image[4] 'Right facing attacks
	Field Attack2Right:Image[4]
	Field Attack3Right:Image[5]
	Field JumpRight:Image[3] 'Jump
	Field JumpLeft:Image[3]
	Field FallRight:Image[3] 'Fall
	Field FallLeft:Image[3]
	Field DeathLeft:Image[11] 'Death
	Field DeathRight:Image[11]
	Field HitLeft:Image[4] 'Receive damage
	Field HitRight:Image[4]
	Field TransformRight:Image[12] 'Transform
	Field TransformLeft:Image[12]
	'Arrays for storing sprites (Flame Warrior Mode)
	Field Fire_IdleLeft:Image[8] 'Idle
	Field Fire_IdleRight:Image[8]
	Field Fire_RunLeft:Image[8] 'Run
	Field Fire_RunRight:Image[8]
	Field Fire_Attack1Left:Image[4] 'Left facing attacks
	Field Fire_Attack2Left:Image[4]
	Field Fire_Attack3Left:Image[5]
	Field Fire_Attack1Right:Image[4] 'Right facing attacks
	Field Fire_Attack2Right:Image[4]
	Field Fire_Attack3Right:Image[5]
	Field Fire_JumpRight:Image[3] 'Jump
	Field Fire_JumpLeft:Image[3]
	Field Fire_FallRight:Image[3] 'Fall
	Field Fire_FallLeft:Image[3]
	Field Fire_DeathLeft:Image[11] 'Death
	Field Fire_DeathRight:Image[11]
	Field Fire_HitLeft:Image[4] 'Receive Damage
	Field Fire_HitRight:Image[4]
	
	Method New()
		'Loading in the sounds
		Swing1 = LoadSound("QuickSwing1.ogg") 'Normal attack 1
		Swing2 = LoadSound("QuickSwing2.ogg") 'Normal attack 2
		Swing3 = LoadSound("QuickSwing3.ogg") 'Normal attack 3
		FireSwing1 = LoadSound("FlameSword1.ogg") 'Flame Warrior attack 1
		FireSwing2 = LoadSound("FlameSword2.ogg") 'Flame Warrior attack 2
		FireSwing3 = LoadSound("FlameSword3.ogg") 'Flame Warrior attack 3
		TransformStart = LoadSound("Transform1.ogg") 'At the start of a transformation
		TransformFinish = LoadSound("Transform2.ogg") 'Towards the end of the transformation.
		Jumping = LoadSound("Jump.ogg") 'Jump
		Landing = LoadSound("Landing.ogg") 'Land on floor
		Damaged1 = LoadSound("Slash4.ogg") 'Player was hit 1
		Damaged2 = LoadSound("Slash5.ogg") 'Player was hit 2
		'Loading in the sprite sheets
		LeftSheet = LoadImage("Warrior_Left.png") 'Left
		RightSheet = LoadImage("Warrior_Right.png") 'Right
		Fire_LeftSheet = LoadImage("FireWarrior_Left.png") 'Fire (Left)
		Fire_RightSheet = LoadImage("FireWarrior_Right.png") 'Fire (Right)
		'Cutting the Normal Mode sheets, and storing the individual sprites in their respective arrays
		CutSpriteSheet(RightSheet, LeftSheet, Attack1Right, Attack1Left, 4, 230, 160, 1) 'Attack1
		CutSpriteSheet(RightSheet, LeftSheet, Attack2Right, Attack2Left, 4, 230, 160, 2) 'Attack2
		CutSpriteSheet(RightSheet, LeftSheet, Attack3Right, Attack3Left, 5, 230, 160, 3) 'Attack3
		CutSpriteSheet(RightSheet, LeftSheet, DeathRight, DeathLeft, 11, 230, 160, 7) 'Death
		CutSpriteSheet(RightSheet, LeftSheet, HitRight, HitLeft,  4, 230, 160, 8) 'Hit
		CutSpriteSheet(RightSheet, LeftSheet, IdleRight, IdleLeft, 8, 230, 160, 9) 'Idle
		CutSpriteSheet(RightSheet, LeftSheet, JumpRight, JumpLeft, 3, 230, 160, 12) 'Jump
		CutSpriteSheet(RightSheet, LeftSheet, FallRight, FallLeft, 3, 230, 160, 14) 'Fall
		CutSpriteSheet(RightSheet, LeftSheet, RunRight, RunLeft, 8, 230, 160, 17) 'Run
		CutSpriteSheet(RightSheet, LeftSheet, TransformRight, TransformLeft, 12, 230, 160, 19) 'Transform into Flame Warrior
		'Cutting the Flame Warrior Sheets, and storing the individual sprites in their respective arrays
		CutSpriteSheet(Fire_RightSheet, Fire_LeftSheet, Fire_Attack1Right, Fire_Attack1Left, 4, 230, 160, 1) 'Attack1
		CutSpriteSheet(Fire_RightSheet, Fire_LeftSheet, Fire_Attack2Right, Fire_Attack2Left, 4, 230, 160, 2) 'Attack2
		CutSpriteSheet(Fire_RightSheet, Fire_LeftSheet, Fire_Attack3Right, Fire_Attack3Left, 5, 230, 160, 3) 'Attack3
		CutSpriteSheet(Fire_RightSheet, Fire_LeftSheet, Fire_DeathRight, Fire_DeathLeft, 11, 230, 160, 7) 'Death
		CutSpriteSheet(Fire_RightSheet, Fire_LeftSheet, Fire_HitRight, Fire_HitLeft,  4, 230, 160, 8) 'Hit
		CutSpriteSheet(Fire_RightSheet, Fire_LeftSheet, Fire_IdleRight, Fire_IdleLeft, 8, 230, 160, 10) 'Idle
		CutSpriteSheet(Fire_RightSheet, Fire_LeftSheet, Fire_JumpRight, Fire_JumpLeft, 3, 230, 160, 12) 'Jump
		CutSpriteSheet(Fire_RightSheet, Fire_LeftSheet, Fire_FallRight, Fire_FallLeft, 3, 230, 160, 14) 'Fall
		CutSpriteSheet(Fire_RightSheet, Fire_LeftSheet, Fire_RunRight, Fire_RunLeft, 8, 230, 160, 16) 'Run
		'Setting the CurrentSprite to its default state
		CurrentSprite = IdleRight[0]
	End Method
	
	Method Animate()
		Select State
			Case "IDLE" 'Plays when standing still
				Local Index:Int = AnimationTick /5 Mod 8 'Every 5 frames, the next sprite will be drawn (there are 8 sprites)
				If FlameWarrior = False Then 'Normal warrior
					If Direction = "RIGHT" Then CurrentSprite = IdleRight[Index]
					If Direction = "LEFT" Then CurrentSprite = IdleLeft[Index]
				Else 'Flame warrior
					If Direction = "RIGHT" Then CurrentSprite = Fire_IdleRight[Index]
					If Direction = "LEFT" Then CurrentSprite = Fire_IdleLeft[Index]
				End
			Case "RUN" 'Plays when moving left/right
				Local Index:Int = AnimationTick /5 Mod 8 'every 5 frames, next sprite is drawn (8 different sprites)
				If FlameWarrior = False Then 'Normal warrior
					If Direction = "RIGHT" Then CurrentSprite = RunRight[Index]
					If Direction = "LEFT" Then CurrentSprite = RunLeft[Index]
				Else 'Flame warrior
					If Direction = "RIGHT" Then CurrentSprite = Fire_RunRight[Index]
					If Direction = "LEFT" Then CurrentSprite = Fire_RunLeft[Index]
				End
			Case "ATTACK1" 'Plays for the first attack
				Local Index:Int = AnimationTick /5 Mod 4 'every 5 frames, next sprite is drawn (4 different sprites)
				If FlameWarrior = False Then 'Normal warrior
					If Direction = "RIGHT" Then CurrentSprite = Attack1Right[Index]
					If Direction = "LEFT" Then CurrentSprite = Attack1Left[Index]
				Else 'Flame warrior
					If Direction = "RIGHT" Then CurrentSprite = Fire_Attack1Right[Index]
					If Direction = "LEFT" Then CurrentSprite = Fire_Attack1Left[Index]
				End
			Case "ATTACK2" 'Plays on the second consecutive attack
				Local Index:Int = AnimationTick /5 Mod 4 'every 5 frames, next sprite is drawn (4 different sprites)
				If FlameWarrior = False Then 'Normal warrior
					If Direction = "RIGHT" Then CurrentSprite = Attack2Right[Index]
					If Direction = "LEFT" Then CurrentSprite = Attack2Left[Index]
				Else 'Flame warrior
					If Direction = "RIGHT" Then CurrentSprite = Fire_Attack2Right[Index]
					If Direction = "LEFT" Then CurrentSprite = Fire_Attack2Left[Index]
				End
			Case "ATTACK3" 'Plays on the third consecutive spacebar attack
				Local Index:Int = AnimationTick /5 Mod 5 'every 5 frames, next sprite is drawn (5 different sprites)
				If FlameWarrior = False Then 'Normal warrior
					If Direction = "RIGHT" Then CurrentSprite = Attack3Right[Index]
					If Direction = "LEFT" Then CurrentSprite = Attack3Left[Index]
				Else 'Flame warrior
					If Direction = "RIGHT" Then CurrentSprite = Fire_Attack3Right[Index]
					If Direction = "LEFT" Then CurrentSprite = Fire_Attack3Left[Index]
				End
			Case "JUMP" 'Plays when midair and moving up.
				Local Index:Int = AnimationTick /5 Mod 3 'every 5 frames, next sprite is drawn (3 different sprites)
				If FlameWarrior = False Then 'Normal warrior
					If Direction = "RIGHT" Then CurrentSprite = JumpRight[Index]
					If Direction = "LEFT" Then CurrentSprite = JumpLeft[Index]
				Else 'Flame warrior
					If Direction = "RIGHT" Then CurrentSprite = Fire_JumpRight[Index]
					If Direction = "LEFT" Then CurrentSprite = Fire_JumpLeft[Index]
				End
			Case "FALL" 'Plays when midair and moving down.
				Local Index:Int = AnimationTick /5 Mod 3 'every 5 frames, next sprite is drawn (3 different sprites)
				If FlameWarrior = False Then 'Normal warrior
					If Direction = "RIGHT" Then CurrentSprite = FallRight[Index]
					If Direction = "LEFT" Then CurrentSprite = FallLeft[Index]
				Else 'Flame warrior
					If Direction = "RIGHT" Then CurrentSprite = Fire_FallRight[Index]
					If Direction = "LEFT" Then CurrentSprite = Fire_FallLeft[Index]
				End
			Case "HIT" 'Plays when receiving damage
				Local Index:Int = AnimationTick /5 Mod 4 'every 5 frames, next sprite is drawn (4 different sprites)
				If FlameWarrior = False Then 'Normal warrior
					If Direction = "RIGHT" Then CurrentSprite = HitRight[Index]
					If Direction = "LEFT" Then CurrentSprite = HitLeft[Index]
				Else 'Flame warrior
					If Direction = "RIGHT" Then CurrentSprite = Fire_HitRight[Index]
					If Direction = "LEFT" Then CurrentSprite = Fire_HitLeft[Index]
				End
			Case "TRANSFORM" 'Plays when transforming into the flame warrior.
				Local Index:Int = AnimationTick /7 Mod 12 'every 7 frames, next sprite is drawn (12 different sprites)
				If Direction = "RIGHT" Then CurrentSprite = TransformRight[Index]
				If Direction = "LEFT" Then CurrentSprite = TransformLeft[Index]
			Case "DYING" 'Plays when all health has been lost
				Local Index:Int = AnimationTick /5 Mod 11 'every 5 frames, next sprite is drawn (11 different sprites)
				If FlameWarrior = False Then 'Normal warrior
					If Direction = "RIGHT" Then CurrentSprite = DeathRight[Index]
					If Direction = "LEFT" Then CurrentSprite = DeathLeft[Index]
				Else 'Flame warrior
					If Direction = "RIGHT" Then CurrentSprite = Fire_DeathRight[Index]
					If Direction = "LEFT" Then CurrentSprite = Fire_DeathLeft[Index]
				End
				If Index = 9 Then State = "DEAD"
			Case "DEAD" 'The only way to leave the dead state is to restart the game or reattempt the level.
				'No need to check for flame warrior, as the last death sprite of both flame warrior and normal warrior are the same.
				If Direction = "RIGHT" Then CurrentSprite = DeathRight[10]
				If Direction = "LEFT" Then CurrentSprite = DeathLeft[10]
		End Select
		AnimationTick += 1 'Increment frame counter by 1	
	End Method
	
	Method Update(LeftPressed:Int, RightPressed:Int, UpPressed:Int, DownPressed:Int, SpacePressed:Int, QPressed:Int, enemy_xpos:Int, enemy_ypos:Int, enemy_width:Int, enemy_height:Int)
		ApplyGravity()
		UpdatePosition()
		HandleMagicEnergy(QPressed)
		'Player doesn't have access to movement/direction logic if they are dying/dead, have been hit or are currently transforming.
		If State <> "DYING" And State <> "DEAD" And State <> "HIT" And State <> "TRANSFORM" Then 
			'First, we deal with logic/controls of player on the ground
			If Midair = False Then ApplyGroundLogic(LeftPressed, RightPressed, UpPressed, DownPressed, SpacePressed, enemy_xpos, enemy_ypos, enemy_width, enemy_height)
			'Now we can deal with the logic/controls of the player while midair
			If Midair = True Then ApplyAirLogic(LeftPressed, RightPressed, UpPressed, DownPressed, SpacePressed)
		End
		If State = "HIT" Then
			dx = 0 'make sure there is no horizontal speed.
			Local currentFrame:Int = AnimationTick /5 Mod 4 'every 5 frames, next sprite is drawn (4 different sprites)
			If currentFrame = 0 Then 
				If ChannelState(6) = 0 Then 'If channel 6 is empty,
					PlaySound(Damaged1, 6, 0) 'At the beginning of the hit animation, play the damaged1 sound within AudioChannel 6, once.
				Else
					PlaySound(Damaged2, 7, 0) 'At the beginning of the hit animation, play the damaged2 sound within AudioChannel 7, once.
				End
			End
			If currentFrame = 3 Then State = "IDLE"
		End
		If State = "TRANSFORM" Then
			dx = 0 'Removing any horizontal speed.
			Local currentFrame:Int = AnimationTick /5 Mod 12 'Every 5 frames, next sprite is drawn (12 different sprites). We can keep track of which frame we are on with this.
			If currentFrame = 0 Then PlaySound(TransformStart, 4, 0) 'At the beginning of the transformation animation, play the TransformStart sound within AudioChannel 4, once.
			If currentFrame = 8 Then PlaySound(TransformFinish, 5, 0) 'Towards the end of the transformation animation, play the TransformFinish sound within AudioChannel 5, once.
			If currentFrame = 11 Then
				State = "IDLE" 'To remove the player from the TRANSFORM state.
				MagicEnergy = 0 'MagicEnergy should be deducted.
				FlameWarrior = True
				Damage = 40 'this is twice the original value
				Speed = 8 'the normal speed is 4.
			End
		End
	End Method
	
	'This method is used for when the player is on the ground.
	Method ApplyGroundLogic(LeftPressed:Int, RightPressed:Int, UpPressed:Int, DownPressed:Int, SpacePressed:Int, enemy_xpos:Int, enemy_ypos:Int, enemy_width:Int, enemy_height:Int)
		Select AttackStage
			Case 0 'Not attacking
				If AttackCooldown > 0 Then AttackCooldown -= 1
				'If neither of the left/right keys (or both of the left/right keys) are pressed, then horizontal movement stops and player is set to idle.
				If LeftPressed = 0 And RightPressed = 0 Or LeftPressed = 1 And RightPressed = 1 Then 'Checking for invalid key combinations.
					State = "IDLE" 'Stand still
					dx = 0 'No horizontal movement.
				End
				'Handle left key press.
				If LeftPressed = 1 Then
					Direction = "LEFT"
					State = "RUN"
					dx = -Speed
				End
				'Handle right key press
				If RightPressed = 1 Then
					Direction = "RIGHT"
					State = "RUN"
					dx = Speed
				End
				'Handle jump
				If UpPressed = 1 Then Jump()
				'Handle transition to Attack1
				If SpacePressed = 1 And AttackCooldown = 0 Then
					AnimationTick = 0 'reset the AnimationTick to get the start of an attack animation. this will not affect jumping as it can only happen while on the ground.
					AttackStage = 1
					CanTransferDamage = True 'moving up a stage, therefore re-enable cantransferdamage
					dx = 0 'We don't want the player to move while attacking.
					State = "ATTACK1" 'First attack
				End
			Case 1 'Attack1
				Local Index:Int = AnimationTick /5 Mod 4 'every 5 frames, next sprite is drawn (4 different sprites)
				If CanTransferDamage = True And Index > 0 Then AttackCollisionHandler(enemy_xpos, enemy_ypos, enemy_width, enemy_height) 'create time by waiting 1 frame between damage dealt.
				If Index = 0 And FlameWarrior = False Then PlaySound(Swing1, 1, 0) 'Normal warrior: Play the swing1 sound within AudioChannel 1, once.
				If Index = 0 And FlameWarrior = True Then PlaySound(FireSwing1, 1,0) 'Flame warrior: Play the fireswing1 sound within AudioChannel 1, once.
				If Index = 3 Then 'When the last frame of the attack animation has played.
					If SpacePressed = 1 Then
						AnimationTick = 0
						CanTransferDamage = True 'moving up a stage, therefore re-enable cantransferdamage
						AttackStage = 2
						State = "ATTACK2" 'Second attack
					Else 'Spacebar isnt being pressed, therefore the attack is reset.
						AttackStage = 0 'Not attacking
						AttackCooldown = 30 'Cooldown applied.
					End
				End
			Case 2 'Attack2
				Local Index:Int = AnimationTick /5 Mod 4 'every 5 frames, next sprite is drawn (4 different sprites)
				If CanTransferDamage = True And Index > 0 Then AttackCollisionHandler(enemy_xpos, enemy_ypos, enemy_width, enemy_height)
				If Index = 0 And FlameWarrior = False Then PlaySound(Swing2, 2, 0) 'Normal warrior: Play the swing2 sound within AudioChannel 2, once.
				If Index = 0 And FlameWarrior = True Then PlaySound(FireSwing2, 2,0) 'Flame warrior: Play the fireswing2 sound within AudioChannel 2, once.
				If Index = 3 Then 'When the last frame of the attack animation has played.
					If SpacePressed = 1 Then
						AnimationTick = 0
						CanTransferDamage = True 'moving up a stage, therefore re-enable cantransferdamage
						AttackStage = 3
						State = "ATTACK3"
					Else 'Spacebar isnt being pressed, therefore the attack is reset.
						AttackStage = 0 'Not attacking
						AttackCooldown = 30 'Cooldown applied.
					End
				End
			Case 3 'Attack3 (final attack)
				Local Index:Int = AnimationTick /5 Mod 5 'every 5 frames, next sprite is drawn (5 different sprites)
				If CanTransferDamage = True And Index > 0 Then AttackCollisionHandler(enemy_xpos, enemy_ypos, enemy_width, enemy_height)
				If Index = 0 And FlameWarrior = False Then PlaySound(Swing3, 3, 0) 'Normal warrior: Play the swing3 sound within AudioChannel 3, once.
				If Index = 0 And FlameWarrior = True Then PlaySound(FireSwing3, 3,0) 'Flame warrior: Play the fireswing3 sound within AudioChannel 3, once.
				If Index = 4 Then 'When the last frame of the attack animation has played.
					AttackStage = 0 'Not attacking
					AttackCooldown = 30 'Cooldown applied.
				End
		End Select
	End Method
	
	
	'This method is used when the player is midair.
	Method ApplyAirLogic(LeftPressed:Int, RightPressed:Int, UpPressed:Int, DownPressed:Int, SpacePressed:Int)
		'If neither of the left/right keys (or both of the left/right keys) are pressed, then horizontal movement stops
		If LeftPressed = 0 And RightPressed = 0 Or LeftPressed = 1 And RightPressed = 1 Then dx = 0 'Checking for invalid key combinations.
		'Handle left key press.
		If LeftPressed = 1 Then
			Direction = "LEFT"
			dx = -Speed
		End
		'Handle right key press
		If RightPressed = 1 Then
			Direction = "RIGHT"
			dx = Speed
		End
		'Handles transitioning to fall state.
		If dy < -1 Then State = "FALL" 'Negative values of dy mean the player is moving down. They are therefore falling.
	End Method
	
	'This method allows the player to jump
	Method Jump()
		If Midair = False Then 'dy will not be set to 13 if midair is already true.
			State = "JUMP"
			Midair = True 'midair is set back to false by OnPosition method.
			dy = 14 'Initial vertical speed. 
			PlaySound(Jumping, 0, 0) 'Play the jump sound within AudioChannel 0, once.
		End
	End Method
	
	'This method brings the player back down to the ground.
	Method ApplyGravity()
		If Midair = True Then 'Only works when player is midair.
			If AnimationTick Mod 2 = 0 Then dy -= 1 'Every 2 frames, decrease vertical velocity by 1.
		End
	End Method
	
	Method UpdatePosition()
		If y > 407 Then 'If the player surpasses y=407, reset them to y=407.
			y = 407 'y=407 is the floor.
			Midair = False 'no longer midair as player is touching the floor.
			dy = 0 'stop any vertical speed.
			PlaySound(Landing, 0, 0) 'Play the landing sound within AudioChannel 0, once.
		End
		x += dx 'Add any existing horizontal speed
		y -= dy 'Add any existing vertical speed.
		'Window Boundaries: The window size is 1200x720.
		If x > 1065 Then x = 1065 'Right side of the window.
		If x  < -100  Then x = -100 'Left side of the window.
	End Method
	
	Method AttackCollisionHandler(enemy_xpos:Int, enemy_ypos:Int, enemy_width:Int, enemy_height:Int)
		'Measuring from player head to maximum possible position reached by sword, checking if enemy body is touched.
		If Direction = "RIGHT" Then 
			If Collides(x+120, y+34, 60, 92, enemy_xpos, enemy_ypos, enemy_width, enemy_height) Then 'Collides will return either true or false
				PendingDamageDealt += Damage 'Player Damage value added to pending damage. Main program handles this.
				If MagicEnergy < 100 And FlameWarrior = False Then MagicEnergy += 10 'Awarding magic energy to the player
				If MagicEnergy > 100 Then MagicEnergy = 100 'Fixing the magic energy in the event it surpasses 100 due to awarding more energy.
				CanTransferDamage = False 'Stop damage from being dealt every frame.
			End
		'Slightly different coordinate positions apply for different directions.
		Elseif Direction = "LEFT" Then 
			If Collides(x+20, y+34, 80, 92, enemy_xpos, enemy_ypos, enemy_width, enemy_height) Then 'Collides will return either true or false
				PendingDamageDealt += Damage 'Player Damage value added to pending damage. Main program handles this.
				If MagicEnergy < 100 And FlameWarrior = False Then MagicEnergy += 10 'Awarding magic energy to the player
				If MagicEnergy > 100 Then MagicEnergy = 100 'Fixing the magic energy in the event it surpasses 100 due to awarding more energy.
				CanTransferDamage = False 'Stop damage from being dealt every frame.
			End
		End
	End Method
	
	Method ReceiveDamage(enemyDamage:Int)
		If State <> "DYING" And State <> "DEAD" Then 'Prevent receiving damage, and prevent dying state from being entered again through receiving damage.
			'Deduct health and enter hit state
			State = "HIT"
			AnimationTick = 0 'resetting this so that we get the start of the hit animation.
			Health -= enemyDamage
			'Checking for death
			If Health <= 0 Then '
				Health = 0 'Restore to 0 if it has turned negative.
				State = "DYING"
				dx = 0 'Cannot move while dying.
				AnimationTick = 0 'reset this so that we get the start of the dying animation.
			End
		End
	End Method
	
	Method HandleMagicEnergy(QPressed:Int)
		Select FlameWarrior
			Case False 'NORMAL MODE
				'It takes 0.3 seconds to display 18 frames (at 60fps). It will therefore take 30 seconds to generate 100 magic energy.
				If AnimationTick Mod 18 = 0 And MagicEnergy < 100 And State <> "TRANSFORM" Then MagicEnergy += 1
				If QPressed = 1 And Midair = False And MagicEnergy = 100 And State <> "TRANSFORM" Then 'Player cannot transform midair.
					AnimationTick = 0 'This won't affect time to charge as magic energy is set to 0 after this anyways.
					FlameWarriorDuration = 600 'The number of frames that the flame warrior mode will last for. 600 frames at 60fps is equal to 10 seconds.
					State = "TRANSFORM" 'Where the transformation will begin.
				End
			Case True 'FLAME WARRIOR MODE
				FlameWarriorDuration -= 1 'Every frame, this must be decremented by one.
				If FlameWarriorDuration = 0 Then 
					FlameWarrior = False 'Checking if the duration is finished.
					Speed = 6 'Original value for speed.
					Damage = 20 'Original value for damage.
				End
		End Select
	End Method
	
	'This method can be called when the player dies, or a new level begins.
	Method Reset()
		State = "IDLE" 'Our default state.
		Health = 100 'Health replenished
		MagicEnergy = 0 'Energy reset
		FlameWarrior = False 'FlameWarrior Disabled
		Damage = 20 'Damage reset to normal.
		Speed = 6 'Speed reset to normal
		x = 60 'X coordinate set to normal
		y = 407 'Y coordinate set to normal
	End Method
	
End Class

'This is the base enemy class. My specific enemies will inherit this class.
Class Enemy
	'==========[POSITION VARIABLES]==========
	Field x:Int 'The x position of the enemy.
	Field y:Int 'The y position of the enemy.
	'==========[MOVEMENT VARIABLES]==========
	Field dx:Int 'This is horizontal speed. dx is a mathematical term meaning change in x.
	'==========[LOGIC VARIABLES]==========
	Field CurrentSprite:Image 'This will be drawn to the screen.
	Field State:String = "IDLE" 'The current state of the enemy.
	Field Direction:String = "RIGHT" 'Which direction the enemy is facing.
	Field AnimationTick:Int = 0 'A frame counter, used for various things such as animations.
	Field PendingDamageDealt:Int = 0 'My main program will automatically transfer any pending damage dealt to the hero, and then remove it.
	Field CanTransferDamage:Bool = True 'Prevents damage from being transferred every frame.
	Field CooldownTime:Int = 0 'Number of frames after an attack that an enemy must idle for.
	'==========[STATS]==========
	Field Health:Int 'How much health the enemy has.
	Field Damage:Int 'How much damage the enemy can deal.
	'==========[SPRITE SHEETS]==========
	Field RightSheet:Image 'Sprites face right here.
	Field LeftSheet:Image 'Sprites face left here.
	'==========[SOUNDS]================
	Field Damaged1:Sound 'Low pitch slash sound
	Field Damaged2:Sound 'Medium pitch slash sound
	Field Damaged3:Sound 'High pitch slash sound
	Field Swing:Sound 'Swinging sound.
	
	'Speed is added to the current position using the UpdatePosition method.
	Method UpdatePosition()
		x += dx 'Increment horizontal position by horizontal velocity.
	End Method
	
	'Main program will use this method when there is pending damage to be received from the player.
	Method ReceiveDamage(playerDamage:Int, Dodge:Bool)
		If State <> "DYING" And State <> "DEAD" Then 'Prevent receiving damage, and prevent dying state from being entered again through receiving damage.
			If Dodge = False Then
				'Deduct health
				Health -= playerDamage
				State = "HIT"
				AnimationTick = 0 'Reset this to get the start of the animation.
				If ChannelState(10) = 0 Then 'If channel 10 is empty, meaning no enemy hit sounds are playing
					PlaySound(Damaged1, 10, 0) 'Play the first enemy hit sound within AudioChannel 10, just once.
				Else 'Channel 10 is not empty, meaning the first enemy hit sound is playing.
					If ChannelState(11) = 0 Then 'If channel 11 is empty
						PlaySound(Damaged2, 11, 0) 'Play the second enemy hit sound within AudioChannel 11, just once.
					Else 'The first two channels are full, meaning the first two hit sounds are currently playing.
						PlaySound(Damaged3, 12, 0) 'Play the third enemy hit sound. within AudioChannel 12, just once.
					End
				End
			Elseif Dodge = True Then 'Dodge has been activated, and enemy wont take damage or enter hit state.
				If Direction = "LEFT" Then 'If enemy is facing left, it means player is left.
					x -= 250 'Teleport to the left to place the enemy behind the player.
				Elseif Direction = "RIGHT" Then 'If the enemy is facing right, it means the player is on the right.
					x += 250 'Teleport to the right to place the enemy in front of the player.
				End
				State = "RUN" 'To prevent the enemy from attacking after teleporting.
			End	
			'Checking for death
			If Health <= 0 Then '
				Health = 0 'Restore to 0 if it has turned negative.
				State = "DYING"
				AnimationTick = 0 'reset this so that we get the start of the dying animation.
			End
		End
	End Method
	
	'This method is continuously called to ensure the enemy is always facing the player.
	Method UpdateDirection(playerx:Int, playerx_offset:Int, enemyx_offset:Int, minimumDistance:Int)
		'Offsets are required as the enemy/player is visibly further forward then the x coordinate is, due to padding (empty pixels) around the sprites.
		If State = "IDLE" Or State = "RUN" Then 'Changing direction can only occur when idle or running
			If Abs(x + enemyx_offset - (playerx + playerx_offset)) > minimumDistance Then 'Distance between players must be greater than the minimum distance to change direction.
				If x + enemyx_offset < playerx + playerx_offset Then Direction = "RIGHT" 'The player is to the right of the enemy, so the enemy should face right.
				If x + enemyx_offset > playerx + playerx_offset Then Direction = "LEFT" 'The player is to the left of the enemy, so the enemy should face left.
			End
		End
	End Method
	
	'This method calculates the distance between the player and the enemy. If the distance between them is small enough, it means the enemy is close enough to attack.
	Method WithinAttackRange(playerx:Int, playerx_offset:Int, enemyx_offset:Int, maximumDistance:Int)
		If Abs(x+enemyx_offset - (playerx + playerx_offset)) < maximumDistance Then 'Distance must be less than 100 between player and enemy to attack.
			Return True 'Meaning that the enemy is indeed within attack range
		Else
			Return False 'Meaning that the enemy is too far away, and should therefore not attack.
		End
	End Method
End Class

Class Skeleton Extends Enemy
	'Arrays representing the different states and directions. Skeleton sprites will be stored in each one.
	Field IdleLeft:Image[4] '4 sprites
	Field IdleRight:Image[4] '4 sprites
	Field RunLeft:Image[12] '12 sprites
	Field RunRight:Image[12] '12 sprites
	Field AttackLeft:Image[13] '13 sprites
	Field AttackRight:Image[13] '13 sprites
	Field DeathLeft:Image[13] '13 sprites
	Field DeathRight:Image[13] '13 sprites
	Field HitLeft:Image[3] '3 sprites
	Field HitRight:Image[3] '3 sprites.
	'Additional sounds
	Field Swing2:Sound 'Second swinging sound
	
	'Used to create a new instance of the skeleton class.
	Method New()
		'Initialise x and y positions
		x = 900
		y = 391
		'Initialise the health of the skeleton and damage that the skeleton will deal.
		Health = 300
		Damage = 20
		'Load the sounds
		Damaged1 = LoadSound("Slash1.ogg") 'Enemy gets hit once
		Damaged2 = LoadSound("Slash2.ogg") 'Enemy gets hit twice
		Damaged3 = LoadSound("Slash3.ogg") 'Enemy gets hit the third time.
		Swing = LoadSound("SideSwing1.ogg") 'Skeleton Attack1
		Swing2 = LoadSound("SideSwing2.ogg") 'Skeleton Attack2
		'Load the sprite sheets
		LeftSheet = LoadImage("Skeleton_Left.png") 'Left facing direction
		RightSheet = LoadImage("Skeleton_Right.png") 'Right facing direction
		'Cut the sheets and store respective sprites in their respective arrays
		CutSpriteSheet(RightSheet, LeftSheet, AttackRight, AttackLeft, 13, 192, 192, 1) 'Attack
		CutSpriteSheet(RightSheet, LeftSheet, DeathRight, DeathLeft, 13, 192, 192, 2) 'Death
		CutSpriteSheet(RightSheet, LeftSheet, RunRight, RunLeft, 12, 192, 192, 3) 'Run
		CutSpriteSheet(RightSheet, LeftSheet, IdleRight, IdleLeft, 4, 192, 192, 4) 'Idle
		CutSpriteSheet(RightSheet, LeftSheet, HitRight, HitLeft, 3, 192, 192, 5) 'Hit 
		'Default state for current sprite being drawn.
		CurrentSprite = IdleRight[0]
	End Method
	
	Method Animate() 'Used to pick the correct enemy sprite to render.
		Select State
			Case "IDLE" 'Plays when the enemy is standing still
				Local Index:Int = AnimationTick /10 Mod 4 'Every 10 frames, the next sprite will be drawn (there are 4 sprites)
				If Direction = "RIGHT" Then CurrentSprite = IdleRight[Index]
				If Direction = "LEFT" Then CurrentSprite = IdleLeft[Index]
			Case "RUN" 'Plays when the enemy is running
				Local Index:Int = AnimationTick /5 Mod 12 'Every 10 frames, the next sprite will be drawn (there are 12 sprites)
				If Direction = "RIGHT" Then CurrentSprite = RunRight[Index]
				If Direction = "LEFT" Then CurrentSprite = RunLeft[Index]
			Case "ATTACK" 'Plays when the enemy is attacking (swinging sword)
				Local Index:Int = AnimationTick /5 Mod 13 'Every 5 frames, the next sprite will be drawn (there are 13 sprites)
				If Direction = "RIGHT" Then CurrentSprite = AttackRight[Index]
				If Direction = "LEFT" Then CurrentSprite = AttackLeft[Index]
			Case "HIT" 'Plays when the enemy receives damage
				Local Index:Int = AnimationTick /5 Mod 3 'Every 5 frames, the next sprite will be drawn (there are 3 sprites)
				If Direction = "RIGHT" Then CurrentSprite = HitRight[Index]
				If Direction = "LEFT" Then CurrentSprite = HitLeft[Index]
			Case "DYING" 'Plays when the enemy has lost all their health
				Local Index:Int = AnimationTick /6 Mod 13 'Every 6 frames, the next sprite will be drawn (there are 13 sprites)
				If Direction = "RIGHT" Then CurrentSprite = DeathRight[Index]
				If Direction = "LEFT" Then CurrentSprite = DeathLeft[Index]
				If Index = 12 Then State = "DEAD"
			Case "DEAD" 'Plays after the DYING  animation finishes.
				If Direction = "RIGHT" Then CurrentSprite = DeathRight[12] 'The last sprite is used for both left and right.
				If Direction = "LEFT" Then CurrentSprite = DeathLeft[12] 'Enemy only needs to play the dying animation once.
		End Select
		AnimationTick += 1 'Increment frame counter. Without this, the enemy sprite will not change.
	End Method
		 
	Method Update(playerx:Int, playery:Int, playerhealth:Int)
		If State <> "DYING" And State <> "DEAD" And playerhealth <> 0 Then 'The skeleton is not allowed to update if dying or dead, or if player is no longer alive
			UpdateDirection(playerx, 57, 45, 10) 'Decides whether the skeleton faces left or right.
			UpdatePosition() 'Updates the x position of the skeleton.
			If CooldownTime > 0 Then CooldownTime -= 1 'Decrement cooldown time by 1 every frame, if there is a cooldown present.
			Select State
				Case "IDLE" 'During the idle state.
					If CooldownTime = 0 Then State = "RUN" 'When the cooldown is over, the skeleton will begin moving.
				Case "RUN" 'During the run state.
					If Direction = "RIGHT" Then dx = 3 'A positive horizontal speed if the enemy is facing right.
					If Direction = "LEFT" Then dx = -3 'A negative horizontal speed if the enemy is facing left.
					If WithinAttackRange(playerx, 114, 85, 100) Then 'Check if the distance between the skeleton and player is close enough to attack.
						CooldownTime = 30 'This will take 30 frames to reach 0. At 60 frames per second, this is a cooldown of 0.5 seconds.
						State = "ATTACK" 'Enter the attack state.
						CanTransferDamage = True 'Damage transfer is allowed.
						AnimationTick = 0 'This is reset so we get the start of an attack animation.
					End
				Case "ATTACK" 'During the attack state.
					dx = 0 'Enemy cannot move while attacking
					Local currentFrame:Int = AnimationTick / 5 Mod 13 'There are 13 frames (index 0 to 12).
					If currentFrame = 0 Then PlaySound(Swing, 8, 0) 'Play the first swing sound within AudioChannel 8, just once.
					If currentFrame = 4 Or currentFrame = 5 Then 'These two sprites are visibly swinging.
						If CanTransferDamage = True Then AttackCollisionHandler(playerx, playery)
					End
					If currentFrame = 6 Then 'Reenable CanTransferDamage in preparation for next attack.
						CanTransferDamage = True 'must reenable it if it was disabled during sprite index 4/5
						PlaySound(Swing2, 9, 0) 'Play the second swing sound within AudioChannel 9, just once.
					End
					If currentFrame = 8 Or currentFrame = 9 Then 'These two sprites are also visibly swinging.
						If CanTransferDamage = True Then AttackCollisionHandler(playerx, playery)
					End
					If currentFrame = 12 Then State = "IDLE" 'The attack is over, and the enemy becomes idle and waits off their cooldown.
				Case "HIT" 'When the enemy has taken damage.
					dx = 0 'Enemy cannot move while hit
					Local currentFrame:Int = AnimationTick / 5 Mod 3 ' There are 3 sprites (index 0-2).
					If currentFrame = 2 Then State = "IDLE" 'The end of the hit animation.
			End Select
		End
		If playerhealth = 0 Then State = "IDLE" 'When the player dies, make the enemy stand still.
	End Method
	
	Method AttackCollisionHandler(player_xpos:Int, player_ypos:Int)
		'Measuring from player head to maximum possible position reached by sword, checking if enemy body is touched.
		If Direction = "RIGHT" Then
			If Collides(x+72, y+24, 120, 110, player_xpos+90, player_ypos+20, 50, 110) Then 'Collides will return either true or false.
				PendingDamageDealt += Damage 'Enemy Damage value added to pending damage. Main program handles this.
				CanTransferDamage = False 'Stop damage from being dealt every frame.
			End
		Elseif Direction = "LEFT" Then
			If Collides(x+5, y+24, 126, 110, player_xpos+80, player_ypos+30, 60, 130) Then 'Collides will return either true or false.
				PendingDamageDealt += Damage 'Enemy Damage value added to pending damage. Main program handles this.
				CanTransferDamage = False 'Stop damage from being dealt every frame.
			End
		End
	End Method
	
	'Can be called when player dies, or player moves to next level.
	Method Reset()
		State = "IDLE" 'Default state.
		Health = 300 'Health reset to original
		x = 900 'X Coordinate reset
		y = 391 'Y Coordinate reset
	End Method
	
End Class

Class Ninja Extends Enemy
	'Additional sprite sheets. These have different padding and must be dealt with differently.
	Field AttackRightSheet:Image
	Field AttackLeftSheet:Image
	'Arrays to store the ninja's various sprites in. Each one represents a particular direction and state combination
	Field IdleLeft:Image[8] '8 sprites
	Field IdleRight:Image[8] '8 sprites
	Field RunLeft:Image[10] '10 sprites
	Field RunRight:Image[10] '10 sprites
	Field AttackLeft:Image[8] '8 sprites
	Field AttackRight:Image[8] '8 sprites
	Field DeathLeft:Image[16] '16 sprites
	Field DeathRight:Image[16] '16 sprites
	Field HitLeft:Image[7] '7 sprites
	Field HitRight:Image[7] '7 sprites.
	Method New()
		'Initialising the x and y positions.
		x = 900
		y = 390
		'Initialising the health of the ninja and damage dealt by the ninja.
		Health = 400
		Damage = 20
		'Load the sounds
		Damaged1 = LoadSound("Slash1.ogg") 'Enemy gets hit once
		Damaged2 = LoadSound("Slash2.ogg") 'Enemy gets hit twice
		Damaged3 = LoadSound("Slash3.ogg") 'Enemy gets hit the third time.
		Swing = LoadSound("ElectricSword.ogg") 'Ninja Attack
		'Loading the sprite sheets.
		RightSheet = LoadImage("NinjaRight.png")
		LeftSheet = LoadImage("NinjaLeft.png")
		AttackRightSheet = LoadImage("NinjaAttackRight.png")
		AttackLeftSheet = LoadImage("NinjaAttackLeft.png")
		'Cutting the sprites from the sprite sheets and storing them in their respective arrays.
		CutSpriteSheet(RightSheet, LeftSheet, IdleRight, IdleLeft,  8, 192, 192, 2) 'Idle
		CutSpriteSheet(RightSheet, LeftSheet, RunRight, RunLeft,  10, 192, 192, 3) 'Run
		CutSpriteSheet(RightSheet, LeftSheet, HitRight, HitLeft,  7, 192, 192, 4) 'Hit
		CutSpriteSheet(RightSheet, LeftSheet, DeathRight, DeathLeft,  16, 192, 192, 1) 'Death
		'Each attack sprite has a larger size (384x288 compared to 192x192) due to additional padding.
		'The coordinates that the ninja is rendered at should be temporarily offset while attacking is occuring to ensure the visible position does not change.
		CutSpriteSheet(AttackRightSheet, AttackLeftSheet, AttackRight, AttackLeft, 8, 384, 288, 1) 'Attack
		'Default sprite
		CurrentSprite = IdleRight[0]
	End Method
	
	'Selects the correct ninja sprite to render based on state, direction and number of frames elapsed.
	Method Animate()
		Select State
			Case "IDLE" 'Ninja is standing still
				Local Index:Int = AnimationTick /4 Mod 8 'Every 4 frames, the next sprite will be drawn (there are 8 sprites)
				If Direction = "RIGHT" Then CurrentSprite = IdleRight[Index]
				If Direction = "LEFT" Then CurrentSprite = IdleLeft[Index]
			Case "RUN" 'Ninja is moving left/right
				Local Index:Int = AnimationTick /5 Mod 10 'Every 5 frames, the next sprite will be drawn (there are 10 sprites)
				If Direction = "RIGHT" Then CurrentSprite = RunRight[Index]
				If Direction = "LEFT" Then CurrentSprite = RunLeft[Index]
			Case "ATTACK" 'Ninja is attacking (sword swing)
				Local Index:Int = AnimationTick /4 Mod 8 'Every 4 frames, the next sprite will be drawn (there are 8 sprites)
				If Direction = "RIGHT" Then CurrentSprite = AttackRight[Index]
				If Direction = "LEFT" Then CurrentSprite = AttackLeft[Index]
			Case "HIT" 'Ninja has received damage (hit by player)
				Local Index:Int = AnimationTick /3 Mod 7 'Every 3 frames, the next sprite will be drawn (there are 7 sprites)
				If Direction = "RIGHT" Then CurrentSprite = HitRight[Index]
				If Direction = "LEFT" Then CurrentSprite = HitLeft[Index]
			Case "DYING" 'Ninja has just died.
				Local Index:Int = AnimationTick /4 Mod 16 'Every 4 frames, the next sprite will be drawn (there are 16 sprites)
				If Direction = "RIGHT" Then CurrentSprite = DeathRight[Index]
				If Direction = "LEFT" Then CurrentSprite = DeathLeft[Index]
				If Index = 12 Then State = "DEAD"
			Case "DEAD" 'Ninja has finished dying animation.
				If Direction = "RIGHT" Then CurrentSprite = DeathRight[15]
				If Direction = "LEFT" Then CurrentSprite = DeathLeft[15]
		End Select
		AnimationTick += 1 'Increment ninja frame counter by 1
	End Method
	
	Method Update(playerx:Int, playery:Int, playerhealth:Int)
		If State <> "DYING" And State <> "DEAD" Then 'The ninja is not allowed to update if dying or dead
			UpdateDirection(playerx, 57, 45, 10) 'Decides whether the ninja faces left or right.
			UpdatePosition() 'Updates the x position of the ninja.
			Select State
				Case "IDLE" 'Standing still
					dx = 0 'No movement.
					If CooldownTime > 0 Then CooldownTime -= 1 'If cooldown currently exists, decrement it by one.
					If CooldownTime = 0 And playerhealth <> 0 Then State = "RUN" 'Once cooldown finishes, enemy can begin to run.
				Case "RUN" 'Running
					If Direction = "RIGHT" Then dx = 5 'Move to the right
					If Direction = "LEFT" Then dx = -5 'Move to the left.
					If WithinAttackRange(playerx, 114, 85, 105) And playerhealth <> 0 Then 'Check if the distance between the ninja and player is close enough to attack.
						CooldownTime = 15 'This will take 15 frames to reach 0. At 60 frames per second, this is a cooldown of 0.25 seconds.
						State = "ATTACK" 'Enter the attack state.
						CanTransferDamage = True 'Damage transfer is allowed.
						AnimationTick = 0 'This is reset so we get the start of an attack animation.
					End
				Case "ATTACK" 'During the attack state.
					dx = 0 'Enemy cannot move while attacking
					Local currentFrame:Int = AnimationTick / 4 Mod 8 'There are 8 frames (index 0 to 7). Each frame is shown for 4 frames.
					If currentFrame = 1 Then PlaySound(Swing, 8, 0) 'Play the swing sound within AudioChannel 8, just once.
					'Index 2,3,4 are visibly swinging
					If currentFrame = 2 Or currentFrame = 3 Or currentFrame = 4 Then 'I want the opportunity for damage to be during these three frames.
						If CanTransferDamage = True Then AttackCollisionHandler(playerx, playery)
					End
					If currentFrame = 7 Then State = "IDLE" 'The attack is over, and the enemy becomes idle and waits off their cooldown.
				Case "HIT" 'During the hit state.
					dx = 0 'Enemy cannot move when hit.
					Local currentFrame:Int = AnimationTick / 3 Mod 7 'There are 7 frames (Index 0 to 6). Each frame is shown for 3 frames.
					If currentFrame = 6 Then State = "IDLE"
			End
		End
	End Method
	
	Method AttackCollisionHandler(player_xpos:Int, player_ypos:Int)
		'Measuring from player head to maximum possible position reached by sword, checking if enemy body is touched.
		If Direction = "RIGHT" Then
			If Collides(x+95, y+5, 120, 145, player_xpos+80, player_ypos+30, 65, 85) Then 'Collides will return either true or false.
				PendingDamageDealt += Damage 'Enemy Damage value added to pending damage. Main program handles this.
				CanTransferDamage = False 'Stop damage from being dealt every frame.
			End
		'Considering the left direction.
		Elseif Direction = "LEFT" Then
			If Collides(x-20, y+5, 105, 145, player_xpos+80, player_ypos+30, 65, 85) Then 'Collides will return either true or false.
				PendingDamageDealt += Damage 'Enemy Damage value added to pending damage. Main program handles this.
				CanTransferDamage = False 'Stop damage from being dealt every frame.
			End
		End
	End Method
	
	'Setting state, health and coordinates to how they originally were at the start of the fight.
	Method Reset()
		State = "IDLE" 'Must be set to IDLE, otherwise the ninja will remain DEAD.
		Health = 400 'health reset
		x = 900 'x coordinate reset
		y = 390 'y coordinate reset
	End Method
	
End Class

Class BlueCloak Extends Enemy
	'Additional sheets (not part of main sheet)
	Field HitSheetRight:Image 'Hit animation
	Field HitSheetLeft:Image
	Field DeathSheetRight:Image 'Death animation
	Field DeathSheetLeft:Image
	'==========[ARRAYS TO STORE SPRITES IN]==========
	Field IdleRight:Image[4] 'Idle
	Field IdleLeft:Image[4]
	Field HitRight:Image[5] 'Received damage
	Field HitLeft:Image[5]
	Field RunRight:Image[6] 'Running
	Field RunLeft:Image[6]
	Field AttackRight:Image[20] 'Attacking
	Field AttackLeft:Image[20]
	Field DeathRight:Image[10] 'Dying
	Field DeathLeft:Image[10]
	'=======[METHODS]=========
	Method New()
		'Setting the position
		x = 900
		y = 430
		'Stats
		Health = 600
		Damage = 20
		'Load the sounds
		Damaged1 = LoadSound("Slash1.ogg") 'Enemy gets hit once
		Damaged2 = LoadSound("Slash2.ogg") 'Enemy gets hit twice
		Damaged3 = LoadSound("Slash3.ogg") 'Enemy gets hit the third time.
		Swing = LoadSound("LowSwing.ogg") 'BlueCloak Attack
		'Loading the sprite sheets
		RightSheet = LoadImage("BlueCloak_Right.png") 'Main
		LeftSheet = LoadImage("BlueCloak_Left.png") 'Main
		HitSheetRight = LoadImage("RightHit.png") 'Additional 1
		HitSheetLeft = LoadImage("LeftHit.png") 'Additional 1
		DeathSheetRight = LoadImage("DeathRight.png") 'Additional 2
		DeathSheetLeft = LoadImage("DeathLeft.png") 'Additional 2
		'Cutting out and storing the sprite sheets in their arrays.
		CutSpriteSheet(RightSheet, LeftSheet, IdleRight, IdleLeft, 4, 150, 110, 1) 'Cutting out and storing the Idle Sprites
		CutSpriteSheet(RightSheet, LeftSheet, RunRight, RunLeft, 6, 150, 110, 2) 'Cutting out and storing the Run Sprites
		CutSpriteSheet(RightSheet, LeftSheet, AttackRight, AttackLeft, 7, 150, 110, 5) 'Cutting out and storing the Attack Sprites
		CutSpriteSheet(HitSheetRight, HitSheetLeft, HitRight, HitLeft, 5, 150, 110, 1) 'Cutting Hit sprites from separate sheets
		CutSpriteSheet(DeathSheetRight, DeathSheetLeft, DeathRight, DeathLeft, 10, 150, 110, 1) 'Cutting Death sprites from another separate sheet
		'Creating a default state for current sprite
		CurrentSprite = IdleRight[0]
	End Method
	
	'Animate will change CurrentSprite based on the state, direction and number of frames elapsed.
	Method Animate()
		Select State
			Case "IDLE" 'When character is standing still.
				Local Index:Int = AnimationTick /6 Mod 4 'Every 6 frames, the next sprite will be drawn (there are 4 sprites)
				If Direction = "RIGHT" Then CurrentSprite = IdleRight[Index]
				If Direction = "LEFT" Then CurrentSprite = IdleLeft[Index]
			Case "RUN" 'When character is running.
				Local Index:Int = AnimationTick /6 Mod 6 'Every 6 frames, the next sprite will be drawn (there are 6 sprites)
				If Direction = "RIGHT" Then CurrentSprite = RunRight[Index]
				If Direction = "LEFT" Then CurrentSprite = RunLeft[Index]
			Case "ATTACK" 'When character is attacking.
				Local Index:Int = AnimationTick /5 Mod 6 'Every 5 frames, the next sprite will be drawn (there are 6 sprites)
				If Direction = "RIGHT" Then CurrentSprite = AttackRight[Index]
				If Direction = "LEFT" Then CurrentSprite = AttackLeft[Index]
			Case "HIT" 'When the character receives damage
				Local Index:Int = AnimationTick /1 Mod 5 'Every 1 frame, the next sprite will be drawn (there are 6 sprites)
				If Direction = "RIGHT" Then CurrentSprite = HitRight[Index]
				If Direction = "LEFT" Then CurrentSprite = HitLeft[Index]
			Case "DYING" 'When the character has 0 health and is dying
				Local Index:Int = AnimationTick /7 Mod 10 'Every 10 frames, the next sprite will be drawn (there are 10 sprites)
				If Direction = "RIGHT" Then CurrentSprite = DeathRight[Index]
				If Direction = "LEFT" Then CurrentSprite = DeathLeft[Index]
				If Index = 9 Then State = "DEAD"
			Case "DEAD" 'When the dying animation is finished.
				If Direction = "RIGHT" Then CurrentSprite = DeathRight[9]
				If Direction = "LEFT" Then CurrentSprite = DeathLeft[9]
		End Select
		AnimationTick += 1 'Increment frame counter by 1.
	End Method
	
	Method Update(playerx:Int, playery:Int)
		If State = "DYING" Or State = "DEAD" Then 'Enemy not allowed to move if dead.
			dx = 0 'Prevent horizontal movement.
		Else 'If the enemy is dead, prevent any updating or movement. Else, carry on as normal.
			UpdateDirection(playerx, 120, 77, 5) 'Decides which direction the enemy should face
			UpdatePosition() 'Used for updating coordinates
			Select State
				Case "IDLE" 'Standing still
					If CooldownTime > 0 Then CooldownTime -= 1 'Decrement cooldown if there is an active cooldown
					If CooldownTime = 0 Then State = "RUN" 'If there is no cooldown, allow the enemy to run
				Case "RUN" 'Running
					'Check if within range before applying any movement speed
					If WithinAttackRange(playerx, 120, 77, 75) Then 
						State = "ATTACK"
						CanTransferDamage = True 'This is disabled after any successful hit made by the BlueCloak, so it should be enabled again.
						CooldownTime = 10 'The enemy will wait 10 frames after attacking
						AnimationTick = 0 'Reset this to get the start of an animation.
					Else
						If Direction = "LEFT" Then dx = -6 'Move left
						If Direction = "RIGHT" Then dx = 6 'Move right
					End
				Case "ATTACK" 'Swinging sword.
					dx = 0 'Not allowed to move while attacking.
					Local Index:Int = AnimationTick/5 Mod 7
					If Index = 0 Then PlaySound(Swing, 8, 0) 'At the start of the attack, play the swing sound within AudioChannel 8, just once.
					'Index 3 and 4 contains frames where the sword is visibly swinging.
					'Therefore, these are the ideal frames for transferring damage to the player.
					If CanTransferDamage = True Then
						If Index = 3 Or Index = 4 Then AttackCollisionHandler(playerx, playery) 'For index 3/4, the enemy is visibly swinging. Check for damage here.
					End
					If Index = 6 Then State = "IDLE" 'On the last frame of the attack, make the player idle.
				Case "HIT"
					dx = 0 'Stop all horizontal movement speed.
					Local currentFrame:Int = AnimationTick / 1 Mod 5 'There are 7 frames (Index 0 to 6). Each frame is shown for 3 frames.
					If currentFrame = 4 Then State = "IDLE" 'On the last frame of the animation, transition into the IDLE state, allowing enemy logic to resume like normal.
			End
		End
	End Method
	
	Method AttackCollisionHandler(player_xpos:Int, player_ypos:Int)
		'Checking if the region covering the enemy attack is colliding with the region around the player body.
		If Direction = "RIGHT" Then
			If Collides(x+45, y, 100, 110, player_xpos+80, player_ypos+30, 65, 85) Then 'Collides will return either true or false.
				PendingDamageDealt += Damage 'Enemy Damage value added to pending damage. Main program handles this.
				CanTransferDamage = False 'Stop damage from being dealt every frame.
			End
		'Considering the left direction.
		Elseif Direction = "LEFT" Then
			If Collides(x+5, y, 100, 110, player_xpos+80, player_ypos+30, 65, 85) Then 'Collides will return either true or false.
				PendingDamageDealt += Damage 'Enemy Damage value added to pending damage. Main program handles this.
				CanTransferDamage = False 'Stop damage from being dealt every frame.
			End
		End
	End Method
	
	'Used to restore the blue cloak to their original stats.
	Method Reset()
		State = "IDLE" 'Must be set to IDLE, otherwise the ninja will remain DEAD.
		Health = 600 'Resetting the health
		x = 900 'Resetting x
		y = 430 'Resetting y
	End Method
	
End Class

Class Healthbars
	'Portraits for every character in the game
	Field WarriorPortrait:Image 'Player
	Field FlameWarriorPortrait:Image 'Player with special ability active
	Field SkeletonPortrait:Image 'Enemy1
	Field NinjaPortrait:Image 'Enemy2
	Field BlueCloakPortrait:Image 'Enemy3
	'Health and energy bars [player]
	Field PlayerHealth_Full:Image 'Full health bar
	Field PlayerHealth_Current:Image 'Current health bar
	Field PlayerEnergy_Full:Image 'Full energy bar
	Field PlayerEnergy_Current:Image 'Current energy bar
	Field PlayerActiveEnergy_Full:Image 'Full active energy bar
	Field PlayerActiveEnergy_Current:Image 'Current active energy.
	'Health and energy bars [enemy]
	Field EnemyHealth_Full:Image 'Full health bar
	Field EnemyHealth_Current:Image 'Current health bar
	Field EnemyEnergy_Full:Image 'Full energy bar
	'Note: I do not intend on making use of energy for the enemy
	'Therefore, I will use the energy bar as a placeholder.
	
	Method New()
		'Initializing the various portrait types
		WarriorPortrait = LoadImage("PlayerPortrait.png") 'Player
		FlameWarriorPortrait = LoadImage("FlameWarriorPortrait.png") 'Player with special ability
		SkeletonPortrait = LoadImage("SkeletonPortrait.png") 'First enemy
		NinjaPortrait = LoadImage("NinjaPortrait.png") 'Second enemy
		BlueCloakPortrait = LoadImage("BlueCloakPortrait.png") 'Third enemy
		'Loading bar images
		PlayerHealth_Full = LoadImage("PlayerHealthBar.png") 'Health
		PlayerEnergy_Full = LoadImage("PlayerEnergyBar.png") 'Energy
		PlayerActiveEnergy_Full = LoadImage("ActivePlayerEnergy.png") 'Active Energy
		EnemyHealth_Full = LoadImage("EnemyHealthBar.png") 'Health (enemy)
		EnemyEnergy_Full = LoadImage("EnemyEnergyBar.png") 'Energy (enemy)
		'Initializing bars
		PlayerHealth_Current = PlayerHealth_Full 'Current health
		PlayerEnergy_Current = PlayerEnergy_Full 'Current energy
		PlayerActiveEnergy_Current = PlayerActiveEnergy_Full 'Current player active energy
		EnemyHealth_Current = EnemyHealth_Full 'Current enemy health
	End Method
	
	'Calculating how much of the player bars should be displayed.
	Method UpdateBars(PlayerHP:Float, PlayerMaxHP:Float, PlayerMP:Float, PlayerMaxMP:Float, EnemyHP:Float, EnemyMaxHP:Float, PlayerFlameDuration:Float, PlayerMaxFlameDuration:Float)
		'PlayerHP Bar is 174x20 pixels
		Local HPWidth = 174*(PlayerHP/PlayerMaxHP)
		PlayerHealth_Current = PlayerHealth_Full.GrabImage(0,0, HPWidth, 20)
		'PlayerEnergy Bar is 172x19 pixels.
		Local MPWidth = 172*(PlayerMP/PlayerMaxMP)
		PlayerEnergy_Current = PlayerEnergy_Full.GrabImage(0,0, MPWidth, 19)
		'PlayerActiveEnergy Bar is 172x19 pixels
		Local FDWidth = 172*(PlayerFlameDuration/PlayerMaxFlameDuration)
		PlayerActiveEnergy_Current = PlayerActiveEnergy_Full.GrabImage(0,0, FDWidth, 19)
		'EnemyHP Bar is 174x20 pixels
		Local EnemyHPWidth = 174*(EnemyHP / EnemyMaxHP)
		EnemyHealth_Current = EnemyHealth_Full.GrabImage(174-EnemyHPWidth, 0, EnemyHPWidth, 20)
	End Method

End Class

Class SmokeEffect
	'Position of smoke
	Field x:Int
	Field y:Int
	'Smoke image variables
	Field SmokeDisappearSheet:Image 'Sprite sheet
	Field SmokeDisappear:Image[12] 'Array
	Field CurrentSmoke:Image 'Holds current smoke sprite
	'Logic
	Field SmokeEnabled:Bool = False 'Whether or not smoke will be rendered
	Field AnimationTick:Int = 0 'Frame counter used in animation
	'Sound
	Field EnemyDisappear:Sound
	
	Method New()
		'Cutting sprite sheet for smoke, and placing each sprite into the smoke disappear array.
		SmokeDisappearSheet = LoadImage("SmokeDisappear.png")
		Local smokesheetxpos = 0
		For Local num:Int = 0 To 11 'Loop through each sprite
			SmokeDisappear[num] = SmokeDisappearSheet.GrabImage(smokesheetxpos, 0, 192, 192)
			smokesheetxpos += 192 'increment by individual sprite width
		Next
		CurrentSmoke = SmokeDisappear[11] '11 is invisible.
		'Loading the sound for disappearing
		EnemyDisappear = LoadSound("EnemyDisappear.ogg")
	End Method
	
	'Used for animating the smoke, and determining if it should appear or not.
	Method HandleSmoke()
		If SmokeEnabled = True Then 'If smoke is present
			Local Index:Int = AnimationTick /3 Mod 12 'Every 3 frames, the next sprite in a list of 12 sprites will be drawn.
			If Index = 0 Then PlaySound(EnemyDisappear, 14, 0) 'On the first frame of the smoke effect, play the enemy disappear sound in AudioChannel 14, just once.
			CurrentSmoke = SmokeDisappear[Index] 'Set current smoke sprite depending on number of frames elapsed.
			AnimationTick += 1 'Increment frame counter
			If Index = 11 Then SmokeEnabled = False 'Turn off the smoke once it is done.
		Else
			CurrentSmoke = SmokeDisappear[11] 'Set current smoke to invisible sprite
			AnimationTick = 0 'Reset frame counter
		End
	End Method
	
	'Used to update smoke x and y positions
	Method UpdatePosition(newx:Int, newy:Int) 
		x = newx 'New xpos
		y = newy 'New ypos
	End
End Class
	
'Check for collision between two boxes.
Function Collides:Bool (x1:Int, y1:Int, w1:Int, h1:Int, x2:Int, y2:Int, w2:Int, h2:Int)
	If x1 >= (x2 + w2) Or (x1 + w1) <= x2 Then Return False 'Both are the only cases where there are no intersections on the x axis.
	If y1 >= (y2 + h2) Or (y1 + h1) <= y2 Then Return False 'Both are the only cases where there are no intersections on the y axis.
	Return True 'Means there is a collision if the above conditions are not true.
End

'We are passing in both directions of the sprite sheet, the two arrays to store each left/right sprite in, how many sprites in the specified row, and the width/height of each sprite.
Function CutSpriteSheet(rightSheet:Image, leftSheet:Image, rightAnimationList:Image[], leftAnimationList:Image[], spriteCount:Int, spritewidth:Int, spriteheight:Int, rowNumber:Int)
	'Left sheets are reflected versions of right sheets. As a result, the animations are reversed. This is already accounted for by modifying xpos and the incrementing of xpos.
	Local ypos = ( spriteheight*(rowNumber-1) ) 'If the Image had a height of 100, and we were cutting from the first row, the ypos would be 0. From the second row it would be 100.
	'Cutting the right facing sheet
	Local xpos = 0
	For Local num:Int = 0 To spriteCount-1
		rightAnimationList[num] = rightSheet.GrabImage(xpos, ypos, spritewidth, spriteheight)
		xpos += spritewidth 'increment because sheet is right facing
	Next
	'Cutting the left facing sheet
	xpos = leftSheet.Width - spritewidth 'adjusting xpos to be appropriate for the left sheets (which are the reversed ones)
	For Local num:Int = 0 To spriteCount-1
		leftAnimationList[num] = leftSheet.GrabImage(xpos, ypos, spritewidth, spriteheight)
		xpos -= spritewidth 'decrement because sheet is left facing
	Next
End

'Used to record a brand new score along with its corresponding name.
Function WriteToLeaderboard(Score:Int, Name:String)
	Local scores_file:FileStream 'My leaderboard data is stored in a text file
	Local scores_data:String 'File contents stored here.
	'Opening a filestream for read/write updating.
	scores_file = FileStream.Open("D:\Nizar's Data Classification\MonkeyX Working Environment\The Gatekeepers Creed (UM)\Game.data\leaderboard.txt", "a") 
	scores_file.WriteString(String(Score) + "," + Name + "~n") 'Writing the score and name to the text file
	scores_file.Close() 'Closing the file
End Function

'Used to sort high scores into top 10.
Function SortHighScores()
	Local scores_file:FileStream 'My leaderboard data is stored in a file
	Local scores_data:String 'File contents will be stored here
	Local scoresInt:Int
	Local highscoreList:= New IntList
	Local counter:Int 'Used in various for loops throughout this function.
	Local last:Int
	Local itemcount:Int
	'Opening the .txt file and storing its contents in the scores_data variable.
	scores_file = FileStream.Open("D:\Nizar's Data Classification\MonkeyX Working Environment\The Gatekeepers Creed (UM)\Game.data\leaderboard.txt", "r")
	scores_data = scores_file.ReadString()
	scores_file.Close
	'Loop through each score and add it to list
	For Local eachscore:String = Eachin scores_data.Split("~n") 'Split by any new lines.
		scoresInt = Int(eachscore)
		highscoreList.AddLast scoresInt
	Next
	highscoreList.Sort(False)
	counter = 0 'Resetting counter.
	For Local smallest=Eachin highscoreList
		If counter = 10 Then
			last = smallest 'Set last value as smallest.
		End If
		counter += 1 'Increment counter by 1.
	Next
	
	'Loop to initialise arrays
	For Local pointer:= 0 Until 10
		PlayerScoreArray[pointer] = 0 'Set value to 0.
		PlayerNameArray[pointer] = "" 'Set value to empty string.
	Next
	counter = 0 'Set counter value to 0.
	
	For Local score:String = Eachin scores_data.Split("~n") 'Splitting by new lines.
		itemcount = 0 'Set itemcount value to 0.
		For Local item:String = Eachin score.Split(",") 'Splitting by comma, to separate name and score.
			'Check if score is above 10th and adds name and score to arrays
			'Checking the score
			If itemcount = 0 And Int(item) >= last Then PlayerScoreArray[counter] = Int(item)
			'Checking the name
			If itemcount = 1 And PlayerScoreArray[counter] >= last Then PlayerNameArray[counter] = item
			itemcount += 1 'Used to go from score to name, which is then reset at the start of the first for loop.
		Next
		If PlayerScoreArray[counter] >= last And counter < 10 Then counter += 1
	Next
	InsertionSort() 'Perform an insertion sort.
	Return 1
End	

'Insertion sort, used for leaderboard.
Function InsertionSort:Void()
	For Local Index:Int = 1 Until PlayerScoreArray.Length 'For every score in PlayerScoreArray 
		Local value:Int = PlayerScoreArray[Index] 'Take numerical score
		Local item:String = PlayerNameArray[Index] 'Take name
		Local Counter:Int = Index - 1 'Set counter as being one less than index.
		While ((Counter >= 0) And PlayerScoreArray[Counter] < value) 'Main loop
			PlayerScoreArray[Counter+1] = PlayerScoreArray[Counter] 'Adjust position of score value
			PlayerNameArray[Counter+1] = PlayerNameArray[Counter] 'Adjust position of name value
			Counter -= 1 'Decrement counter
		End While
		PlayerScoreArray[Counter+1] = value 'Adjust position of score value
		PlayerNameArray[Counter+1] = item 'Adjust position of name value.
	Next
End Function

Function RequestMusic(RequestedMusic:String)
	If ChannelState(13) = 0 Then 'If the music has been stopped.
		If CurrentMusic = "MENU" Then 'If the current music is menu
			PlaySound(MenuMusic, 13, 1) 'Play menu music at AudioChannel13, and loop it.
		Elseif CurrentMusic = "LEVEL1" Then 'If the current music is Level1
			PlaySound(Level1Music, 13, 1) 'Play Level1 music at AudioChannel13, and loop it.
		Elseif CurrentMusic = "LEVEL2" Then 'If the current music is Level2
			PlaySound(Level2Music, 13, 1) 'Play Level2 music at AudioChannel13, and loop it.
		Elseif CurrentMusic = "LEVEL3" Then 'If the current music is Level3
			PlaySound(Level3Music, 13, 1) 'Play Level3 music at AudioChannel13, and loop it.
		End
		SetChannelVolume(13, 0.5) 'Set AudioChannel13's volume to half the original, to allow SFX to be audible.
	End
	If CurrentMusic <> RequestedMusic Then 'If the requested music is different to the current music
		StopChannel(13) 'Stop the music
		CurrentMusic = RequestedMusic 'Set the music to the requested music
	End
End